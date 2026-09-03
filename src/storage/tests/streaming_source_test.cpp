// L3 流式源测试（chunked-transfer-design.md §8 / 测试 34-38 核心）。
//
// 锚定行为：
//   34 真并行：接收线程独立于消费推进（不 pull 时流仍被收入有界队列）
//   35 有界背压：queue_limit 封顶 + 慢消费下数据一致（TCP 降速路径）
//   36 资源释放：源提前析构（消费中断）→ fd 归还（池可用性）
//   37 流式 vs 整读一致：pull 流字节 == 磁盘 record（DIGEST 根 + 磁盘块 CRC 全过验）
#include <gtest/gtest.h>
#include <storage/cpp/data_service.h>
#include <storage/cpp/data_writer.h>
#include <storage/cpp/decompressing_streambuf.h>
#include <storage/cpp/memory_chunk_source.h>
#include <storage/cpp/network_chunk_source.h>
#include <network/cpp/data_client_pool.h>
#include <network/cpp/message_protocol.h>
#include <network/cpp/message_types.h>
#include <network/cpp/transport_interface.h>
#include <network/cpp/tcp_socket.h>
#include <serialization/cpp/object_header.h>
#include <common/cpp/fly_buffer.h>
#include <common/cpp/test_helpers.h>
#include <common/cpp/data_checksum.h>
#include <core/cpp/config.h>
#include <log/cpp/logger.h>
#include <filesystem>
#include <thread>
#include <chrono>
#include <cstring>
#include <poll.h>

namespace fly {
namespace {

// 组装合法新格式 record（单 raw 块 + trailer）。
FlyBufferPtr make_valid_record(const std::string& data) {
    auto record = CMMakeShared<FlyBuffer>();
    int32_t sz = static_cast<int32_t>(data.size());
    uint64_t crc = data_checksum(data.data(), data.size());
    record->write(reinterpret_cast<const char*>(&sz), 4);
    record->write(reinterpret_cast<const char*>(&sz), 4);
    record->write(reinterpret_cast<const char*>(&crc), 8);
    record->write(data.data(), data.size());
    ObjectHeader header;
    header.total_size_ = data.size();
    header.chunk_count_ = 1;
    header.py_name_ = "bytes";
    header.py_name_len_ = 5;
    header.compression_type_ = 0;
    header.block_comp_lens_ = {static_cast<uint32_t>(data.size())};  // B' 块表
    CMString trailer = header.serialize_trailer();
    record->write(trailer.data(), trailer.size());
    return record;
}

}  // namespace

class StreamingSourceTest : public ::testing::Test {
protected:
    CMString test_dir_;
    CMSharedPtr<DataService> ds_ = DataService::instance();

    void SetUp() override {
        test_dir_ = fly::test::qa_tmp_dir("fly_test_stream");
        std::filesystem::remove_all(test_dir_);
        std::filesystem::create_directories(test_dir_);
        ds_->reset();
    }

    void TearDown() override {
        ds_->stop_data_server();
        std::filesystem::remove_all(test_dir_);
    }

    // 写对象并登记（返回 record 字节用于一致性对照）。
    CMString write_object(const CMString& db_path, const CMString& full,
                          const std::string& data) {
        ds_->register_database(db_path, test_dir_ + "/data");
        ds_->on_write_started(db_path, full);
        DataWriter writer(test_dir_, test_dir_ + "/data", "ssrc", 0);
        auto rec = make_valid_record(data);
        writer.write_record(full, data.size(), 1, *rec, "");
        writer.flush();
        auto entries = writer.get_all_entries(full);
        EXPECT_TRUE(entries.has_value());
        ds_->on_write_completed(db_path, full, entries.value());
        ds_->on_object_flushed(full);
        return CMString(rec->data(), rec->size());
    }
};

// 37（本地部分）：DSSB 流式构造（Memory 源）解压 == 内存构造解压。
TEST_F(StreamingSourceTest, StreamingDssbEqualsMemoryDssb) {
    std::string payload(3000, 'Q');
    auto record = make_valid_record(payload);

    // 内存构造（基线）。
    CMString via_mem;
    {
        DecompressingStreamBuf d1(record->data(), record->size());
        std::istream is(&d1);
        CMString got(payload.size(), '\0');
        is.read(got.data(), payload.size());
        EXPECT_EQ(got, CMString(payload.data(), payload.size()));
        EXPECT_FALSE(d1.checksum_failed());
    }

    // 流式构造（Memory 源 + 块流边界）。
    auto mem = CMMakeShared<MemoryChunkSource>(record->data(), record->size());
    ASSERT_FALSE(mem->failed());
    DecompressingStreamBuf d2(mem, mem->block_area_len());
    std::istream is2(&d2);
    CMString got2(payload.size(), '\0');
    is2.read(got2.data(), payload.size());
    EXPECT_EQ(got2, CMString(payload.data(), payload.size()));
    EXPECT_FALSE(d2.checksum_failed());
    EXPECT_EQ(d2.py_name(), "bytes");
}

// 34+37（网络部分）：真 DataServer 分片流 → NetworkChunkSource → pull 字节流
// 与磁盘 record 一致（DIGEST 根 + 磁盘块 CRC 全过验）；接收线程不等消费
//（先 sleep 再 pull 一次拿全——流已被接收线程收入队列）。
TEST_F(StreamingSourceTest, NetworkSourceRoundtripAndTrueParallelism) {
    std::string payload(400, 'N');  // record > 64（阈值注入）→ 分片路径
    Config::instance()->set_int("chunked_transfer_threshold", 64);
    CMString disk_record = write_object("/stream37", "/stream37:obj", payload);

    ds_->start_data_server("127.0.0.1", 0, 2);
    int port = ds_->get_data_port();

    DataClientPool pool(2);
    auto ex = pool.request_raw_exchange("127.0.0.1", port, "/stream37:obj");
    ASSERT_TRUE(ex.success);
    ASSERT_TRUE(ex.meta.chunked_);
    ASSERT_GT(ex.meta.trailer_len_, 0u);
    ASSERT_EQ(ex.meta.py_name_, "bytes");

    uint64_t block_area = ex.meta.total_compressed_len_ - ex.meta.trailer_len_;
    auto src = CMMakeShared<NetworkChunkSource>(
        pool.transport(), ex.fd, ex.meta,
        [&pool, fd = ex.fd](bool healthy) { pool.release_borrowed_fd(fd, healthy); },
        16 * ex.meta.chunk_frame_bytes_);
    src->start();

    // 真并行锚定：消费端等待期（500ms）内接收线程应已把整个小对象收入队列
    //（loopback 即时送达）——随后 pull 无阻塞一次拿全。
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    CMString pulled;
    pulled.resize(disk_record.size());
    char* p = pulled.data();
    size_t remaining = pulled.size();
    while (remaining > 0) {
        int64_t got = src->pull(p, remaining);
        ASSERT_GT(got, 0) << "pull stalled/died with " << remaining << " remaining";
        p += got;
        remaining -= static_cast<size_t>(got);
    }
    EXPECT_FALSE(src->failed());
    EXPECT_EQ(pulled, disk_record);  // 37：字节级一致

    // 解压一致（磁盘块 CRC 过验）。
    DecompressingStreamBuf d(pulled.data(), pulled.size());
    std::istream is(&d);
    CMString got_payload(payload.size(), '\0');
    is.read(got_payload.data(), payload.size());
    EXPECT_EQ(got_payload, CMString(payload.data(), payload.size()));
    EXPECT_FALSE(d.checksum_failed());
    Config::instance()->set_int("chunked_transfer_threshold", 4194304);
}

// 35：queue_limit 极小（单片）+ 消费端分段延迟 → 数据一致（背压路径：接收
// 线程在队列满时阻塞 q_space_cv_，TCP 流控压住 server——消费推进后继续）。
TEST_F(StreamingSourceTest, BoundedQueueBackpressureConsistency) {
    std::string payload(400, 'B');
    Config::instance()->set_int("chunked_transfer_threshold", 64);
    CMString disk_record = write_object("/stream35", "/stream35:obj", payload);

    ds_->start_data_server("127.0.0.1", 0, 2);
    int port = ds_->get_data_port();

    DataClientPool pool(2);
    auto ex = pool.request_raw_exchange("127.0.0.1", port, "/stream35:obj");
    ASSERT_TRUE(ex.success);

    uint64_t block_area = ex.meta.total_compressed_len_ - ex.meta.trailer_len_;
    // 队列上限压到单片以下（字节级 8）：接收线程必须等消费才能推进。
    auto src = CMMakeShared<NetworkChunkSource>(
        pool.transport(), ex.fd, ex.meta,
        [&pool, fd = ex.fd](bool healthy) { pool.release_borrowed_fd(fd, healthy); },
        /*queue_byte_limit=*/8);
    src->start();

    CMString pulled;
    pulled.resize(disk_record.size());
    char* p = pulled.data();
    size_t remaining = pulled.size();
    while (remaining > 0) {
        int64_t got = src->pull(p, remaining);
        ASSERT_GT(got, 0) << "fail_reason=" << src->fail_reason();
        p += got;
        remaining -= static_cast<size_t>(got);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));  // 慢消费
    }
    EXPECT_FALSE(src->failed());
    EXPECT_EQ(pulled, disk_record);
    (void)block_area;
    Config::instance()->set_int("chunked_transfer_threshold", 4194304);
}

// 36：源提前析构（消费中断）→ fd 归还（池后续请求可用——连接数不泄漏）。
TEST_F(StreamingSourceTest, ResourceReleaseOnEarlyDestruction) {
    std::string payload(400, 'R');
    Config::instance()->set_int("chunked_transfer_threshold", 64);
    write_object("/stream36", "/stream36:obj", payload);

    ds_->start_data_server("127.0.0.1", 0, 2);
    int port = ds_->get_data_port();

    DataClientPool pool(1);  // 单 slot：泄漏即死锁/失败
    {
        auto ex = pool.request_raw_exchange("127.0.0.1", port, "/stream36:obj");
        ASSERT_TRUE(ex.success);
        auto src = CMMakeShared<NetworkChunkSource>(
            pool.transport(), ex.fd, ex.meta,
            [&pool, fd = ex.fd](bool healthy) { pool.release_borrowed_fd(fd, healthy); },
            16 * ex.meta.chunk_frame_bytes_);
        src->start();
        // 消费一小段即弃（模拟 Unpickler 中途异常）。
        char buf[16];
        (void)src->pull(buf, sizeof(buf));
    }  // src 析构：接收线程 join + fd/slot 归还

    // 池仍然可用（slot 已归还）。
    auto r = pool.request("127.0.0.1", port, "/stream36:obj", 0, 0, 5000);
    EXPECT_TRUE(std::get<0>(r)) << "pool slot/fd leaked after early stream destruction";
    Config::instance()->set_int("chunked_transfer_threshold", 4194304);
}

// ════════════════════════════════════════════════════════════════════
// P0-2 NCS 块级状态机（§14.1 A'2）：用脚本化块流 fake server 直喂
// DATA_CHUNK 帧，覆盖坏块 resend 恢复、跨帧块解析、trailer 透传、
// 负长度块头、字节计数对账、重复 DIGEST 等分支。
// ════════════════════════════════════════════════════════════════════

namespace {

struct FakeBlockSpec {
    std::string data;
    bool corrupt = false;  // true：块头 CRC 域注坏（数据字节完好）
};

// 组装块格式 record：N 个 [i32 unc][i32 comp][u64 crc][data] 块 + trailer。
// 返回 record 全字节；trailer_len 出参供 meta.trailer_len_ 使用。
CMString make_block_record(const std::vector<FakeBlockSpec>& blocks, uint64_t* trailer_len) {
    auto record = CMMakeShared<FlyBuffer>();
    CMVector<uint32_t> lens;
    uint64_t total_unc = 0;
    for (const auto& b : blocks) {
        int32_t sz = static_cast<int32_t>(b.data.size());
        uint64_t crc = data_checksum(b.data.data(), b.data.size());
        if (b.corrupt) crc ^= 0x01;
        record->write(reinterpret_cast<const char*>(&sz), 4);
        record->write(reinterpret_cast<const char*>(&sz), 4);
        record->write(reinterpret_cast<const char*>(&crc), 8);
        record->write(b.data.data(), b.data.size());
        lens.push_back(static_cast<uint32_t>(b.data.size()));
        total_unc += b.data.size();
    }
    ObjectHeader header;
    header.total_size_ = total_unc;
    header.chunk_count_ = static_cast<uint32_t>(blocks.size());
    header.py_name_ = "bytes";
    header.py_name_len_ = 5;
    header.compression_type_ = 0;
    header.block_comp_lens_ = lens;
    CMString trailer = header.serialize_trailer();
    *trailer_len = trailer.size();
    record->write(trailer.data(), trailer.size());
    return CMString(record->data(), record->size());
}

// 脚本化块流 server：accept 后按 frames 顺序发 DATA_CHUNK 帧，再按 digest_mode
// 收尾。answer_resend 时读取 CHUNK_RESEND 并按 record 字节重发（fcrc=0 新语义）。
class BlockStreamServer {
public:
    enum class DigestMode {
        IMMEDIATE,             // 发完 frames 直接 DIGEST
        AFTER_RESEND,          // 读 1 个 resend → 重发好字节 → DIGEST
        DOUBLE_AFTER_RESEND,   // 读 resend → 重发 → DIGEST → 再发一个 DIGEST
    };
    struct Frame {
        uint64_t offset;
        CMString bytes;
        uint64_t fcrc;  // 0 = 新语义（client 跳过帧级验证）
    };

    DigestMode digest_mode = DigestMode::IMMEDIATE;
    std::vector<Frame> frames;
    CMString record;  // resend 重发的字节源（好数据）

    explicit BlockStreamServer() {
        listen_fd_ = transport_->create_listen_socket("127.0.0.1", 0);
        port_ = transport_->get_port(listen_fd_);
        thread_ = std::thread([this] { serve(); });
    }
    ~BlockStreamServer() {
        transport_->close(listen_fd_);
        if (thread_.joinable()) thread_.join();
    }
    int port() const { return port_; }
    // 等 server 线程结束（之后 resend_requests 可安全读取）。
    void finish() { if (thread_.joinable()) thread_.join(); }

    // server 线程结束后读取（main 线程 finish() 后访问，无需锁）。
    std::vector<std::pair<uint64_t, uint64_t>> resend_requests;

private:
    void serve() {
        struct pollfd pfd;
        pfd.fd = listen_fd_;
        pfd.events = POLLIN;
        pfd.revents = 0;
        if (::poll(&pfd, 1, 10000) <= 0) return;
        int fd = transport_->accept_connection(listen_fd_);
        if (fd < 0) return;
        transport_->set_recv_timeout(fd, 5000);
        transport_->set_send_timeout(fd, 5000);
        auto send = [&](const CMString& bytes) {
            transport_->send_all(fd, bytes.data(), bytes.size());
        };
        auto send_digest = [&]() {
            DataDigestMessage digest;
            digest.root_crc_ = 0;  // T5：根摘要双侧消除，仅作流终止信号
            digest.chunk_count_ = 1;
            send(MessageProtocol::encode(digest));
        };
        // 读一个 CHUNK_RESEND 请求（记录之；send_reply=false 时不重发数据——
        // 供 DOUBLE 模式控制 DIGEST/重发帧的到达顺序）。
        auto read_resend_request = [&](bool send_reply) -> bool {
            char h[9];
            if (!recv_exact(transport_.get(), fd, h, 9)) return false;
            uint64_t tl = 0;
            if (!parse_frame_header(h, tl)) return false;
            CMString rest(static_cast<size_t>(tl - 1), '\0');
            if (!recv_exact(transport_.get(), fd, rest.data(), static_cast<size_t>(tl - 1)))
                return false;
            CMString frame;
            frame.assign(h, 9);
            frame += rest;
            ChunkResendMessage rs;
            if (!MessageProtocol::decode(frame, rs)) return false;
            resend_requests.emplace_back(rs.offset_, rs.length_);
            if (send_reply) {
                uint64_t n = std::min<uint64_t>(rs.length_, record.size() - rs.offset_);
                send(ChunkFrameProtocol::encode_header(rs.offset_, 0, n));
                send(record.substr(rs.offset_, n));
            }
            return true;
        };

        for (const auto& f : frames) {
            send(ChunkFrameProtocol::encode_header(f.offset, f.fcrc, f.bytes.size()));
            send(f.bytes);
        }
        switch (digest_mode) {
        case DigestMode::IMMEDIATE:
            send_digest();
            break;
        case DigestMode::AFTER_RESEND:
            read_resend_request(/*send_reply=*/true);
            send_digest();
            break;
        case DigestMode::DOUBLE_AFTER_RESEND:
            // 两个 DIGEST 都先于重发帧到达（client 洞未补 → 第一个 DIGEST 触发
            // continue 等待 → 第二个 DIGEST 命中 digest_seen → duplicate digest）。
            read_resend_request(/*send_reply=*/false);
            send_digest();
            send_digest();
            // 尾声补发重发帧（client 已失败，字节无人消费；仅为对端收尾）。
            if (!resend_requests.empty()) {
                uint64_t off = resend_requests[0].first;
                uint64_t n = std::min<uint64_t>(resend_requests[0].second,
                                                record.size() - off);
                send(ChunkFrameProtocol::encode_header(off, 0, n));
                send(record.substr(off, n));
            }
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        transport_->close(fd);
    }

    CMSharedPtr<Transport> transport_ = create_tcp_transport();
    int listen_fd_ = -1;
    int port_ = 0;
    std::thread thread_;
};

// NCS 消费端：连 server → 构造源 → pull 全部期望字节。
// 返回 (pulled_bytes, source)；流失败时 pulled 为已收前缀。
struct NcsPullOutcome {
    CMString pulled;
    CMSharedPtr<NetworkChunkSource> src;
};

NcsPullOutcome run_block_stream(BlockStreamServer& server, const CMString& record,
                                uint64_t trailer_len, size_t expect_len) {
    DataResponseMessage meta;
    meta.chunked_ = true;
    meta.total_compressed_len_ = record.size();
    meta.chunk_frame_bytes_ = 4096;
    meta.py_name_ = "bytes";
    meta.trailer_len_ = trailer_len;

    auto transport = create_tcp_transport();
    int fd = transport->create_connection("127.0.0.1", server.port());
    auto src = CMMakeShared<NetworkChunkSource>(
        transport, fd, meta, [](bool) {}, 1 << 20);
    src->start();

    NcsPullOutcome out;
    out.src = src;
    out.pulled.resize(expect_len);
    char* p = out.pulled.data();
    size_t remaining = expect_len;
    while (remaining > 0) {
        int64_t got = src->pull(p, remaining);
        if (got <= 0) break;  // 流失败/终止：剩余置占位（断言层检查 failed）
        p += got;
        remaining -= static_cast<size_t>(got);
    }
    out.pulled.resize(expect_len - remaining);
    return out;
}

}  // namespace

// 坏块 CRC → CHUNK_RESEND → server 重发好块 → 乱序 pending/drain → 字节一致。
// 覆盖 consume_block 的 hole 登记（213-229）+ deliver_bytes 乱序暂存与
// drain_pending（243-259）+ DIGEST 到达但有洞时继续等重传（277-284）。
TEST_F(StreamingSourceTest, NcsBadBlockResendRecovers) {
    uint64_t good_tl = 0;
    uint64_t wire_tl = 0;
    std::vector<FakeBlockSpec> specs = {{"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", false},
                                        {"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", false}};
    auto good_record = make_block_record(specs, &good_tl);
    specs[1].corrupt = true;  // 块1 CRC 域注坏（数据字节完好）
    auto wire_record = make_block_record(specs, &wire_tl);
    ASSERT_NE(wire_record, good_record);  // 注坏只改 CRC 域 → 两版仅差这 8 字节
    ASSERT_EQ(good_tl, wire_tl);

    const size_t blk = 16 + 32;
    BlockStreamServer server;
    server.record = good_record;
    server.digest_mode = BlockStreamServer::DigestMode::AFTER_RESEND;
    server.frames = {
        {0, wire_record.substr(0, blk), 0},                    // 块0 好
        {blk, wire_record.substr(blk, blk), 0},                // 块1 CRC 坏
        {2 * blk, wire_record.substr(2 * blk), 0},             // trailer
    };

    auto out = run_block_stream(server, good_record, good_tl, good_record.size());
    EXPECT_FALSE(out.src->failed()) << "fail_reason=" << out.src->fail_reason();
    EXPECT_EQ(out.pulled, good_record);
    server.finish();  // 确保 resend_requests 写入完成
    ASSERT_EQ(server.resend_requests.size(), 1u);
    EXPECT_EQ(server.resend_requests[0].first, blk);
    EXPECT_EQ(server.resend_requests[0].second, blk);
}

// 乱序极端形态：坏块0 + 好块1 → hole 在前沿，块1 进 pending；resend 补 0 后
// drain 连续交付两块 + trailer。数据序必须与原 record 完全一致。
TEST_F(StreamingSourceTest, NcsHoleAtFrontDrainsInOrder) {
    uint64_t good_tl = 0;
    uint64_t wire_tl = 0;
    std::vector<FakeBlockSpec> specs = {{"11111111111111111111111111111111", false},
                                        {"22222222222222222222222222222222", false}};
    auto good_record = make_block_record(specs, &good_tl);
    specs[0].corrupt = true;  // 块0 CRC 域注坏（hole 在流前沿）
    auto wire_record = make_block_record(specs, &wire_tl);
    const size_t blk = 16 + 32;

    // resend 重发源 = good 版（模拟盘上数据完好、wire 传输中 CRC 域损坏）。
    BlockStreamServer server;
    server.record = good_record;
    server.digest_mode = BlockStreamServer::DigestMode::AFTER_RESEND;
    server.frames = {
        {0, wire_record.substr(0, blk), 0},               // 块0 CRC 坏 → hole
        {blk, wire_record.substr(blk, blk), 0},           // 块1 好 → pending
        {2 * blk, wire_record.substr(2 * blk), 0},        // trailer（前沿未到 → pending）
    };

    auto out = run_block_stream(server, good_record, good_tl, good_record.size());
    EXPECT_FALSE(out.src->failed()) << "fail_reason=" << out.src->fail_reason();
    EXPECT_EQ(out.pulled, good_record);
    server.finish();
    ASSERT_EQ(server.resend_requests.size(), 1u);
    EXPECT_EQ(server.resend_requests[0].first, 0u);
}

// 跨帧块解析（慢路径累积）+ 跨 block_area 帧分裂（块区尾 + trailer 头同帧）
// + 纯 trailer 帧透传。重组字节流必须与原 record 一致。
TEST_F(StreamingSourceTest, NcsFrameSplitsAndTrailerPassthrough) {
    uint64_t tl = 0;
    std::vector<FakeBlockSpec> specs = {{"cccccccccccccccccccccccccccccccc", false},
                                        {"dddddddddddddddddddddddddddddddd", false}};
    auto record = make_block_record(specs, &tl);
    const size_t blk = 16 + 32;
    const size_t block_area = 2 * blk;
    (void)blk;
    const size_t trailer_len = record.size() - block_area;

    BlockStreamServer server;
    server.record = record;
    server.digest_mode = BlockStreamServer::DigestMode::IMMEDIATE;
    server.frames = {
        // 帧1：块0 前 10B（慢路径：parse_buf 跨帧累积）。
        {0, record.substr(0, 10), 0},
        // 帧2：块0 剩余 + 块1 全部（慢路径收尾 + 快路径整块）。
        {10, record.substr(10, block_area - 10), 0},
        // 帧3：跨界——块1 尾 8B + trailer 前 4B（140-144 跨界分裂路径）。
        {block_area - 8, record.substr(block_area - 8, 12), 0},
        // 帧4：纯 trailer 透传（135-137）。
        {block_area + 4, record.substr(block_area + 4), 0},
    };

    auto out = run_block_stream(server, record, tl, record.size());
    EXPECT_FALSE(out.src->failed()) << "fail_reason=" << out.src->fail_reason();
    EXPECT_EQ(out.pulled, record);
    EXPECT_EQ(trailer_len, static_cast<size_t>(tl));
}

// 负长度块头（快路径 n≥16 与慢路径跨帧两种入口）→ 流立即失败，不出数据。
TEST_F(StreamingSourceTest, NcsNegativeBlockHeaderFails) {
    // 快路径：单帧 ≥16B，comp 域为负。
    {
        CMString bad(16 + 8, '\0');
        int32_t unc = 8;
        int32_t comp = -1;
        std::memcpy(bad.data(), &unc, 4);
        std::memcpy(bad.data() + 4, &comp, 4);

        BlockStreamServer server;
        server.digest_mode = BlockStreamServer::DigestMode::IMMEDIATE;
        server.frames = {{0, bad, 0}};
        DataResponseMessage meta;
        meta.chunked_ = true;
        meta.total_compressed_len_ = bad.size();  // 无 trailer（block_area = total）
        meta.chunk_frame_bytes_ = 4096;
        meta.py_name_ = "bytes";
        meta.trailer_len_ = 0;

        auto transport = create_tcp_transport();
        int fd = transport->create_connection("127.0.0.1", server.port());
        auto src = CMMakeShared<NetworkChunkSource>(
            transport, fd, meta, [](bool) {}, 1 << 20);
        src->start();
        char buf[64];
        int64_t got = src->pull(buf, sizeof(buf));
        EXPECT_TRUE(src->failed());
        EXPECT_EQ(src->fail_reason(), CMString("integrity: corrupt block header (negative sizes)"));
        EXPECT_TRUE(got <= 0) << "负长度块头后不得交付任何字节";
    }
    // 慢路径：16B 头跨两帧，第二帧补齐后 comp 为负。
    {
        CMString bad(16, '\0');
        int32_t unc = 4;
        int32_t comp = -7;
        std::memcpy(bad.data(), &unc, 4);
        std::memcpy(bad.data() + 4, &comp, 4);

        BlockStreamServer server;
        server.digest_mode = BlockStreamServer::DigestMode::IMMEDIATE;
        server.frames = {{0, bad.substr(0, 10), 0}, {10, bad.substr(10), 0}};
        DataResponseMessage meta;
        meta.chunked_ = true;
        meta.total_compressed_len_ = bad.size();
        meta.chunk_frame_bytes_ = 4096;
        meta.py_name_ = "bytes";
        meta.trailer_len_ = 0;

        auto transport = create_tcp_transport();
        int fd = transport->create_connection("127.0.0.1", server.port());
        auto src = CMMakeShared<NetworkChunkSource>(
            transport, fd, meta, [](bool) {}, 1 << 20);
        src->start();
        char buf[64];
        (void)src->pull(buf, sizeof(buf));
        EXPECT_TRUE(src->failed());
        EXPECT_EQ(src->fail_reason(), CMString("integrity: corrupt block header (negative sizes)"));
    }
}

// 字节计数对账：块字节收满前 DIGEST 到达且无洞（server 少发一块）→
// received_ != total → 流失败（295-298），消费端不得拿到不完整数据当成功。
TEST_F(StreamingSourceTest, NcsByteCountMismatchFails) {
    uint64_t tl = 0;
    std::vector<FakeBlockSpec> specs = {{"eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee", false},
                                        {"ffffffffffffffffffffffffffffffff", false}};
    auto record = make_block_record(specs, &tl);
    const size_t blk = 16 + 32;

    BlockStreamServer server;
    server.digest_mode = BlockStreamServer::DigestMode::IMMEDIATE;  // 只发块0 就 DIGEST
    server.frames = {{0, record.substr(0, blk), 0}};

    DataResponseMessage meta;
    meta.chunked_ = true;
    meta.total_compressed_len_ = record.size();  // 声明全量
    meta.chunk_frame_bytes_ = 4096;
    meta.py_name_ = "bytes";
    meta.trailer_len_ = tl;

    auto transport = create_tcp_transport();
    int fd = transport->create_connection("127.0.0.1", server.port());
    auto src = CMMakeShared<NetworkChunkSource>(
        transport, fd, meta, [](bool) {}, 1 << 20);
    src->start();

    CMString pulled;
    pulled.resize(record.size());
    char* p = pulled.data();
    size_t remaining = pulled.size();
    while (remaining > 0) {
        int64_t got = src->pull(p, remaining);
        if (got <= 0) break;
        p += got;
        remaining -= static_cast<size_t>(got);
    }
    EXPECT_TRUE(src->failed());
    EXPECT_EQ(src->fail_reason(), CMString("integrity: byte count mismatch"));
    // 交付量断言不锁定：对账失败（DIGEST 到达）与 pull 消费块 0 存在合法
    // 竞态窗口——对账失败时清空已入队未消费块（零容忍：不完整数据不得
    // 流出），交付量 ∈ {0, 块0} 均为正确行为。核心断言 = 失败必然暴露。
}

// resend 恢复后重复 DIGEST → "duplicate digest frame"（277-281）。
TEST_F(StreamingSourceTest, NcsDuplicateDigestFails) {
    uint64_t tl = 0;
    std::vector<FakeBlockSpec> specs = {{"77777777777777777777777777777777", true}};
    auto record = make_block_record(specs, &tl);
    const size_t blk = 16 + 32;

    BlockStreamServer server;
    server.record = record;
    server.digest_mode = BlockStreamServer::DigestMode::DOUBLE_AFTER_RESEND;
    server.frames = {{0, record.substr(0, blk), 0},
                     {blk, record.substr(blk), 0}};

    auto out = run_block_stream(server, record, tl, record.size());
    EXPECT_TRUE(out.src->failed());
    EXPECT_EQ(out.src->fail_reason(), CMString("integrity: duplicate digest frame"));
    server.finish();
    EXPECT_EQ(server.resend_requests.size(), 1u);
}

// 旧协议端坏帧（fcrc 非 0 且失配）→ 整帧 resend 请求 + 帧内容不得交付。
// 安全断言：无论 resend 恢复成功与否，成功终止时字节必须与原 record 一致
//（决不允许坏帧字节静默混入重组流）。
TEST_F(StreamingSourceTest, NcsLegacyBadFrameNeverDeliversCorruptBytes) {
    uint64_t tl = 0;
    std::vector<FakeBlockSpec> specs = {{"88888888888888888888888888888888", false}};
    auto record = make_block_record(specs, &tl);
    const size_t blk = 16 + 32;

    BlockStreamServer server;
    server.record = record;
    server.digest_mode = BlockStreamServer::DigestMode::IMMEDIATE;
    CMString bad_payload = record.substr(0, blk);
    bad_payload[20] = static_cast<char>(bad_payload[20] ^ 0xFF);  // 帧内字节注坏
    server.frames = {
        {0, bad_payload, data_checksum(  // fcrc 声明按好数据计算 → 失配
             record.substr(0, blk).data(), blk)},
        {blk, record.substr(blk), 0},
    };

    auto out = run_block_stream(server, record, tl, record.size());
    if (!out.src->failed()) {
        EXPECT_EQ(out.pulled, record)
            << "流成功终止时坏帧字节必须已被 resend 替换为好字节";
    } else {
        EXPECT_EQ(out.src->fail_reason(), CMString("integrity: resent block still corrupt"))
            << "坏帧内容喂入解析器导致二次损坏路径";
    }
}

// ════════════════════════════════════════════════════════════════════
// P0-3：真实 DataServer 的 CHUNK_RESEND 服务端路径（此前从未被真 server
// 测试覆盖——现有重传测试全部走 fake server）。
// ════════════════════════════════════════════════════════════════════

// 真实链路：写大对象（分片路径）→ 直接位腐聚合 .dat 的块字节 → NCS 流式读
// 触发块 CRC 失败 → CHUNK_RESEND → server pread 重发（盘上仍是坏字节）→
// client 判二次损坏 → 流失败升格。覆盖 server handle_chunk_resend 的正常
// pread + 重发全链（data_server.cpp 560-634）与 client resend 仍坏路径。
TEST_F(StreamingSourceTest, ServerChunkResendAfterDiskCorruptionFailsClosed) {
    std::string payload(400, 'C');
    Config::instance()->set_int("chunked_transfer_threshold", 64);
    CMString disk_record = write_object("/resend37", "/resend37:obj", payload);

    // 定位聚合文件内该 record 的字节区间，翻转块数据域一个字节（避开块头
    // 16B 与 trailer——精确命中块 CRC 校验）。
    auto [loc_ok, loc] = ds_->find_chunked_location("/resend37:obj");
    ASSERT_TRUE(loc_ok);
    ASSERT_GT(loc.size, 20u);
    {
        std::fstream f(std::string(loc.file_path),
                       std::ios::binary | std::ios::in | std::ios::out);
        ASSERT_TRUE(f.good());
        f.seekg(static_cast<std::streamoff>(loc.offset + 16 + 5));
        char c = 0;
        f.read(&c, 1);
        ASSERT_EQ(f.gcount(), 1);
        f.seekp(static_cast<std::streamoff>(loc.offset + 16 + 5));
        f.put(c ^ static_cast<char>(0x5A));
        f.flush();
    }

    ds_->start_data_server("127.0.0.1", 0, 2);
    int port = ds_->get_data_port();

    DataClientPool pool(2);
    auto ex = pool.request_raw_exchange("127.0.0.1", port, "/resend37:obj");
    ASSERT_TRUE(ex.success);
    ASSERT_TRUE(ex.meta.chunked_);

    auto src = CMMakeShared<NetworkChunkSource>(
        pool.transport(), ex.fd, ex.meta,
        [&pool, fd = ex.fd](bool healthy) { pool.release_borrowed_fd(fd, healthy); },
        16 * ex.meta.chunk_frame_bytes_);
    src->start();

    CMString pulled;
    pulled.resize(disk_record.size());
    size_t received = 0;
    while (received < pulled.size()) {
        int64_t got = src->pull(pulled.data() + received, pulled.size() - received);
        if (got <= 0) break;
        received += static_cast<size_t>(got);
    }
    // 盘上坏字节无法通过 resend 恢复 → 零容忍失败关闭（不交付坏数据）。
    EXPECT_TRUE(src->failed());
    EXPECT_EQ(src->fail_reason(), CMString("integrity: resent block still corrupt"));
    // 单块对象：块坏 = 全部坏 → fail-closed 不得交付任何字节。
    EXPECT_EQ(received, 0u);
    Config::instance()->set_int("chunked_transfer_threshold", 4194304);
}

// 裸客户端直发 ChunkResendMessage 变体：越界区间被拒（无响应字节）、
// 合法区间收到精确重发帧、同一 offset 重复请求 → server 断连防御。
TEST_F(StreamingSourceTest, ServerChunkResendProtocolEnforcement) {
    std::string payload(400, 'E');
    Config::instance()->set_int("chunked_transfer_threshold", 64);
    CMString disk_record = write_object("/resend38", "/resend38:obj", payload);

    ds_->start_data_server("127.0.0.1", 0, 2);
    int port = ds_->get_data_port();

    auto transport = create_tcp_transport();
    int fd = transport->create_connection("127.0.0.1", port);
    ASSERT_GE(fd, 0);
    transport->set_recv_timeout(fd, 5000);
    transport->set_send_timeout(fd, 5000);

    fprintf(stderr, "[DBG] step1 connect done\n");
    // 触发 chunked serve（登记 conn 状态：chunk_file/chunk_off/chunk_size）。
    DataRequestMessage req;
    req.object_name_ = "/resend38:obj";
    CMString req_frame = MessageProtocol::encode(req);
    ASSERT_TRUE(transport->send_all(fd, req_frame.data(), req_frame.size()));
    fprintf(stderr, "[DBG] step2 request sent\n");
    char mh[9];
    ASSERT_TRUE(recv_exact(transport.get(), fd, mh, 9));
    fprintf(stderr, "[DBG] step3 meta header read\n");
    uint64_t mtl = 0;
    ASSERT_TRUE(parse_frame_header(mh, mtl));
    ASSERT_EQ(static_cast<uint8_t>(mh[8]),
              static_cast<uint8_t>(MessageType::DATA_RESPONSE));
    // 读掉 META 帧 rest + 整个 chunk 流（400B=1 帧）+ DIGEST。
    auto read_full = [&](uint64_t n) {
        CMString buf(static_cast<size_t>(n), '\0');
        EXPECT_TRUE(recv_exact(transport.get(), fd, buf.data(), static_cast<size_t>(n)));
        return buf;
    };
    (void)read_full(mtl - 1);
    fprintf(stderr, "[DBG] step4 meta rest read\n");
    // 单个 chunk 帧：[9B 帧头][4B 子头][16B offset/crc][raw 400B]。
    {
        char ch[9];
        ASSERT_TRUE(recv_exact(transport.get(), fd, ch, 9));
        uint64_t ctl = 0;
        ASSERT_TRUE(parse_frame_header(ch, ctl));
        ASSERT_EQ(static_cast<uint8_t>(ch[8]),
                  static_cast<uint8_t>(MessageType::DATA_CHUNK));
        (void)read_full(ctl - 1);
    }
    fprintf(stderr, "[DBG] step5 chunk frame read\n");
    // DIGEST 帧头 + payload。
    char dh[9];
    ASSERT_TRUE(recv_exact(transport.get(), fd, dh, 9));
    uint64_t dtl = 0;
    ASSERT_TRUE(parse_frame_header(dh, dtl));
    (void)read_full(dtl - 1);

    fprintf(stderr, "[DBG] step6 digest read\n");
    // (a) 越界 resend（offset+length > record size）→ server 拒绝，无响应。
    ChunkResendMessage rs;
    rs.offset_ = disk_record.size() - 8;
    rs.length_ = 64;  // 越出 record 尾
    CMString bad_rs = MessageProtocol::encode(rs);
    ASSERT_TRUE(transport->send_all(fd, bad_rs.data(), bad_rs.size()));

    fprintf(stderr, "[DBG] step7 oob resend sent\n");
    // (b) 合法 resend（offset=0, len=16）→ 精确重发帧。
    rs.offset_ = 0;
    rs.length_ = 16;
    CMString good_rs = MessageProtocol::encode(rs);
    ASSERT_TRUE(transport->send_all(fd, good_rs.data(), good_rs.size()));
    {
        char rh[9];
        ASSERT_TRUE(recv_exact(transport.get(), fd, rh, 9))
            << "合法 resend 必须得到重发帧（越界 resend 不得产生响应）";
        uint64_t rtl = 0;
        ASSERT_TRUE(parse_frame_header(rh, rtl));
        ASSERT_EQ(static_cast<uint8_t>(rh[8]),
                  static_cast<uint8_t>(MessageType::DATA_CHUNK));
        CMString rest = read_full(rtl - 1);  // [4B sub][16B offset/crc][16B raw]
        ASSERT_EQ(rest.size(), 36u);
        EXPECT_EQ(read_be32(rest.data()), 16u);  // 子头 small_len
        uint64_t foff = 0, fcrc = 0;
        ChunkFrameProtocol::parse_small_fields(rest.data() + 4, 16, foff, fcrc);
        EXPECT_EQ(foff, 0u);
        EXPECT_EQ(fcrc, 0u);  // 重发帧按新语义发 CRC=0
        CMString raw = rest.substr(20, 16);
        EXPECT_EQ(raw, disk_record.substr(0, 16)) << "pread 字节与盘一致";
    }

    fprintf(stderr, "[DBG] step8 good resend done\n");
    // 注：同 offset 重复 resend 的断连分支（575-579）测试中发现生产代码
    // 自死锁（handle_chunk_resend 持 conn_mutex_ 调 cleanup_fd，后者重入
    // 同锁）——按零容忍纪律停在此处并上报，不收录会挂死进程的用例。
    transport->close(fd);
    Config::instance()->set_int("chunked_transfer_threshold", 4194304);
}

}  // namespace fly
