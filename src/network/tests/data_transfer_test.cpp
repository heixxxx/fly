#include <gtest/gtest.h>
#include <storage/cpp/data_service.h>
#include <storage/cpp/database.h>
#include <storage/cpp/data_writer.h>
#include <storage/cpp/decompressing_streambuf.h>
#include <serialization/cpp/object_header.h>
#include <common/cpp/fly_buffer.h>
#include <common/cpp/data_checksum.h>
#include <core/cpp/config.h>
#include <network/cpp/data_client_pool.h>
#include <network/cpp/tcp_socket.h>
#include <network/cpp/message_protocol.h>
#include <network/cpp/message_types.h>
#include <log/cpp/logger.h>
#include <filesystem>
#include <thread>
#include <atomic>
#include <poll.h>

namespace fly {

// 新格式 record（§4.4）：单 raw 块 [i32 unc][i32 comp][u64 crc][data] + trailer。
namespace {
FlyBufferPtr make_simple_record(const std::string& data, const CMString& py_name) {
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
    header.py_name_ = py_name;
    header.py_name_len_ = static_cast<uint16_t>(py_name.size());
    header.compression_type_ = 0;  // raw passthrough
    CMString trailer = header.serialize_trailer();
    record->write(trailer.data(), trailer.size());
    return record;
}

// 写一个对象并完成登记（盘上有完整合法 record，DataServer 可服务）。
void write_valid_object(DataService* ds, const CMString& test_dir,
                        const CMString& db_path, const CMString& full,
                        const std::string& data) {
    ds->register_database(db_path, test_dir + "/data");
    ds->on_write_started(db_path, full);
    DataWriter writer(test_dir, test_dir + "/data", "xfer", 0);
    auto rec = make_simple_record(data, "bytes");
    writer.write_record(full, data.size(), 1, *rec, "");
    writer.flush();
    auto entries = writer.get_all_entries(full);
    ASSERT_TRUE(entries.has_value());
    ds->on_write_completed(db_path, full, entries.value());
    ds->on_object_flushed(full);
}
}  // namespace


class DataTransferTest : public ::testing::Test {
protected:
    CMString test_dir_;
    CMSharedPtr<DataService> ds_ = DataService::instance();
    CMUniquePtr<Database> db_;

    void SetUp() override {
        test_dir_ = "/tmp/fly_test_transfer_" + std::to_string(::getpid()) + "_" +
                    std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id()));
        std::filesystem::remove_all(test_dir_);
        std::filesystem::create_directories(test_dir_);
        ds_->reset();
    }

    void TearDown() override {
        ds_->stop_data_server();
        std::filesystem::remove_all(test_dir_);
    }
};

TEST_F(DataTransferTest, DataServiceStartDataServer) {
    ds_->start_data_server("127.0.0.1", 0, 2);
    EXPECT_GT(ds_->get_data_port(), 0);
}

TEST_F(DataTransferTest, DataServerReturnsObjectNotFoundForUnknownObject) {
    std::string db_path = "/testb";
    ds_->register_database(db_path, test_dir_ + "/data");

    ds_->start_data_server("127.0.0.1", 0, 2);
    int port = ds_->get_data_port();

    DataClientPool pool(4);
    auto [success, data, py_name, hash, error, rerr] = pool.request(
        "127.0.0.1", port, db_path + ":nonexistent", 0, 0, 5000);

    EXPECT_FALSE(success);
    EXPECT_EQ(rerr, ReadError::OBJECT_NOT_FOUND);
}

TEST_F(DataTransferTest, DataServerReturnsDataForCompletedWrite) {
    std::string db_path = "/testc";
    ds_->register_database(db_path, test_dir_ + "/data");

    std::string full = db_path + ":myobj";
    std::string test_data = "hello world test data";

    ds_->on_write_started(db_path, full);

    auto record = make_simple_record(test_data, "bytes");

    DataWriter writer(test_dir_, test_dir_ + "/data", "test", 0);
    writer.write_record(full, test_data.size(), 1, *record, "");
    writer.flush();

    auto entries = writer.get_all_entries(full);
    ASSERT_TRUE(entries.has_value());
    ds_->on_write_completed(db_path, full, entries.value());
    ds_->on_object_flushed(full);

    EXPECT_EQ(ds_->has_local_object(full), true);

    ds_->start_data_server("127.0.0.1", 0, 2);
    int port = ds_->get_data_port();

    DataClientPool pool(4);
    auto [success, data, py_name, hash, error, rerr] = pool.request(
        "127.0.0.1", port, full, 0, 0, 5000);

    EXPECT_TRUE(success) << "error: " << error;
    EXPECT_FALSE(!data || data->empty());
}

// After the read-path hardening change, the pool does NOT internally retry
// DATA_NOT_READY — it returns immediately so the TIER2 layer owns retry policy.
TEST_F(DataTransferTest, DataClientPoolReturnsDataNotReadyImmediately) {
    std::string db_path = "/testd";
    ds_->register_database(db_path, test_dir_ + "/data");

    std::string full = db_path + ":myobj";

    ds_->on_write_started(db_path, full);

    ds_->start_data_server("127.0.0.1", 0, 2);
    int port = ds_->get_data_port();

    DataClientPool pool(2);
    auto start = std::chrono::steady_clock::now();
    auto [success, data, py_name, hash, error, rerr] = pool.request(
        "127.0.0.1", port, full, 0, 0, 30000);
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now() - start).count();

    // Must return promptly (well under the previous 100ms poll interval) and
    // classify the cause as DATA_NOT_READY.
    EXPECT_FALSE(success);
    EXPECT_EQ(rerr, ReadError::DATA_NOT_READY);
    EXPECT_LT(elapsed_ms, 50);
}

TEST_F(DataTransferTest, DataServerHandlesConcurrentRequestsBeyondThreadCount) {
    std::string db_path = "/teste";
    ds_->register_database(db_path, test_dir_ + "/data");

    for (int i = 0; i < 6; ++i) {
        std::string name = "obj_" + std::to_string(i);
        std::string full = db_path + ":" + name;
        std::string test_data = "data for object " + std::to_string(i);

        ds_->on_write_started(db_path, full);

        auto record = make_simple_record(test_data, "bytes");

        DataWriter writer(test_dir_, test_dir_ + "/data", "w" + std::to_string(i), 0);
        writer.write_record(full, test_data.size(), 1, *record, "");
        writer.flush();

        auto entries = writer.get_all_entries(full);
        ASSERT_TRUE(entries.has_value());
        ds_->on_write_completed(db_path, full, entries.value());
        ds_->on_object_flushed(full);
    }

    ds_->start_data_server("127.0.0.1", 0, 2);
    int port = ds_->get_data_port();

    DataClientPool pool(4);

    std::atomic<int> success_count(0);
    CMVector<std::thread> threads;
    for (int i = 0; i < 6; ++i) {
        threads.emplace_back([&, i]() {
            auto [ok, data, py_name, hash, error, rerr] = pool.request(
                "127.0.0.1", port, db_path + ":obj_" + std::to_string(i), 0, 0, 10000);
            if (ok) success_count.fetch_add(1);
        });
    }
    for (auto& t : threads) t.join();

    EXPECT_EQ(success_count.load(), 6);
}

// DataServer echoes a NET_PROBE_REQUEST as a NET_PROBE_RESPONSE carrying the
// requested payload size. This is the server half of the bandwidth probe used
// by the network-aware read-priority feature.
TEST_F(DataTransferTest, DataServerEchoesNetProbeRequest) {
    ds_->start_data_server("127.0.0.1", 0, 2);
    int port = ds_->get_data_port();

    auto transport = create_tcp_transport();
    int fd = transport->create_connection("127.0.0.1", port);
    ASSERT_GE(fd, 0);
    transport->set_recv_timeout(fd, 5000);
    transport->set_send_timeout(fd, 5000);

    NetProbeRequestMessage req;
    req.payload_size_ = 8192;
    req.probe_seq_ = 42;
    CMString encoded = MessageProtocol::encode(req);
    ASSERT_TRUE(transport->send_all(fd, encoded.data(), encoded.size()));

    // Read 9B frame prefix [8B header][1B type].
    char hdr[9];
    size_t got = 0;
    while (got < 9) {
        ssize_t n = transport->recv(fd, hdr + got, 9 - got);
        ASSERT_GT(n, 0);
        got += static_cast<size_t>(n);
    }
    uint64_t total_len = 0;
    ASSERT_TRUE(parse_frame_header(hdr, total_len));
    ASSERT_EQ(static_cast<uint8_t>(hdr[8]),
              static_cast<uint8_t>(MessageType::NET_PROBE_RESPONSE));

    // total_len = 1(type, already read) + payload_len. Read the remaining
    // payload bytes, then reassemble the full frame for decode.
    uint64_t payload_len = total_len - 1;
    CMString rest(static_cast<size_t>(payload_len), '\0');
    size_t rgot = 0;
    while (rgot < payload_len) {
        ssize_t n = transport->recv(fd, rest.data() + rgot, static_cast<size_t>(payload_len) - rgot);
        ASSERT_GT(n, 0) << "recv rest failed at offset " << rgot << "/" << payload_len;
        rgot += static_cast<size_t>(n);
    }
    CMString frame;
    frame.assign(hdr, 9);
    frame += rest;

    NetProbeResponseMessage resp;
    ASSERT_TRUE(MessageProtocol::decode(frame, resp));
    EXPECT_EQ(resp.probe_seq_, 42u);
    EXPECT_EQ(resp.payload_.size(), 8192u);

    transport->close(fd);
}

// ════════════════════════════════════════════════════════════════════
// L2 分片传输（chunked-transfer-design.md §4.5 / 测试 23-27）
// ════════════════════════════════════════════════════════════════════

namespace {
// 测试侧独立组一个 DATA_CHUNK 帧（帧头 + type + 子头 + seq/crc + raw）。
CMString make_chunk_frame(uint32_t seq, uint64_t crc, const char* data, size_t n) {
    CMString hdr = ChunkFrameProtocol::encode_header(seq, crc, n);
    CMString frame = hdr;
    frame.append(data, n);
    return frame;
}
}  // namespace

// 测试 23：跨多片大 payload 分片收发一致（真 DataServer，阈值注入 64B）。
TEST_F(DataTransferTest, ChunkedRoundtrip) {
    Config::instance()->set_int("chunked_transfer_threshold", 64);

    std::string db_path = "/chunkrt";
    std::string full = db_path + ":big";
    // 300 字节 payload：2 块 64B 磁盘块 + trailer → record > 64 → 分片路径，
    // 每片 4MB 切片（300B = 1 片）…… 300 < 4MB 只有一片！
    // 分片数 = ceil(record_size / 4MB)——小对象只有 1 片，测不到多片重组。
    // 改为注入更小的片尺寸不可行（协议常量）——多片验证由 fake server
    // 测试 24-26 覆盖（任意片尺寸手工组帧）；本测试验证真 server 的
    // 分片路径端到端（META/CHUNK/DIGEST 全链）+ 数据一致。
    std::string payload(300, 'Z');
    write_valid_object(ds_.get(), test_dir_, db_path, full, payload);

    ds_->start_data_server("127.0.0.1", 0, 2);
    int port = ds_->get_data_port();

    DataClientPool pool(2);
    auto [success, data, py_name, hash, error, rerr] =
        pool.request("127.0.0.1", port, full, 0, 0, 5000);

    Config::instance()->set_int("chunked_transfer_threshold", 4194304);

    ASSERT_TRUE(success) << "error: " << error;
    ASSERT_TRUE(data && !data->empty());
    EXPECT_EQ(py_name, "bytes");

    // 重组 record 解压一致（DecompressingStreamBuf 直接消费，§4.5）。
    DecompressingStreamBuf dsbuf(data->data(), data->size());
    std::istream is(&dsbuf);
    CMString got(300, '\0');
    is.read(got.data(), 300);
    EXPECT_EQ(got, CMString(payload.data(), payload.size()));
    EXPECT_FALSE(dsbuf.checksum_failed());
}

// 测试 27：小对象（< 阈值）仍走整帧快路径（回归）。
TEST_F(DataTransferTest, SmallObjectFastPathWithChunkingEnabled) {
    Config::instance()->set_int("chunked_transfer_threshold", 64);

    std::string db_path = "/smallfp";
    std::string full = db_path + ":tiny";
    std::string payload = "tiny";  // record < 64 → 快路径
    write_valid_object(ds_.get(), test_dir_, db_path, full, payload);

    ds_->start_data_server("127.0.0.1", 0, 2);
    int port = ds_->get_data_port();

    DataClientPool pool(2);
    auto [success, data, py_name, hash, error, rerr] =
        pool.request("127.0.0.1", port, full, 0, 0, 5000);

    Config::instance()->set_int("chunked_transfer_threshold", 4194304);

    ASSERT_TRUE(success) << "error: " << error;
    ASSERT_TRUE(data && !data->empty());
    EXPECT_EQ(py_name, "bytes");
}

// fake 分片 server：可控注错（坏片 CRC / 坏 resend / 错 digest）。
// 每请求三片（"AA.."、"BB.."、"CC.."），按注入策略组流。
class FakeChunkServer {
public:
    enum class FailMode { NONE, BAD_CHUNK_THEN_GOOD_RESEND, BAD_CHUNK_STILL_BAD, BAD_DIGEST };
    FailMode mode = FailMode::NONE;

    explicit FakeChunkServer() {
        listen_fd_ = transport_->create_listen_socket("127.0.0.1", 0);
        port_ = transport_->get_port(listen_fd_);
        thread_ = std::thread([this] { serve(); });
    }
    ~FakeChunkServer() {
        transport_->close(listen_fd_);
        if (thread_.joinable()) thread_.join();
    }
    int port() const { return port_; }

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

        // 读 DATA_REQUEST。
        char h[9];
        if (!recv_exact(transport_.get(), fd, h, 9)) return;
        uint64_t tl = 0;
        if (!parse_frame_header(h, tl)) return;
        CMString reqbuf(static_cast<size_t>(tl - 1), '\0');
        if (!recv_exact(transport_.get(), fd, reqbuf.data(), static_cast<size_t>(tl - 1))) return;

        // 三片 payload。
        const std::string chunks[3] = {std::string(16, 'A'), std::string(16, 'B'),
                                       std::string(16, 'C')};
        const uint64_t total = 48;
        const uint64_t frame = 16;

        // META。
        DataResponseMessage meta;
        meta.success_ = true;
        meta.chunked_ = true;
        meta.total_compressed_len_ = total;
        meta.chunk_frame_bytes_ = frame;
        meta.py_name_ = "bytes";
        CMString meta_frame = DataResponseProtocol::encode(meta, nullptr).header_segment;
        transport_->send_all(fd, meta_frame.data(), meta_frame.size());

        // CHUNK 流（seq=1 注坏）。
        fly::DataChecksum root;
        for (uint32_t seq = 0; seq < 3; ++seq) {
            const auto& c = chunks[seq];
            uint64_t crc = data_checksum(c.data(), c.size());
            if (seq == 1 && mode != FakeChunkServer::FailMode::NONE) {
                crc ^= 0x01;  // 注坏
            }
            CMString frame_bytes = make_chunk_frame(seq, crc, c.data(), c.size());
            transport_->send_all(fd, frame_bytes.data(), frame_bytes.size());
            root.update(c.data(), c.size());
        }

        // DIGEST。
        uint64_t root_crc = root.final();
        if (mode == FakeChunkServer::FailMode::BAD_DIGEST) root_crc ^= 0x01;
        DataDigestMessage digest;
        digest.root_crc_ = root_crc;
        digest.chunk_count_ = 3;
        CMString df = MessageProtocol::encode(digest);
        transport_->send_all(fd, df.data(), df.size());

        // 等可能的 CHUNK_RESEND。
        if (mode == FakeChunkServer::FailMode::BAD_CHUNK_THEN_GOOD_RESEND ||
            mode == FakeChunkServer::FailMode::BAD_CHUNK_STILL_BAD) {
            char rh[9];
            if (!recv_exact(transport_.get(), fd, rh, 9)) return;
            uint64_t rtl = 0;
            if (!parse_frame_header(rh, rtl)) return;
            CMString rbuf(static_cast<size_t>(rtl - 1), '\0');
            if (!recv_exact(transport_.get(), fd, rbuf.data(), static_cast<size_t>(rtl - 1))) return;
            // decode 消费完整帧（9B 前缀 + payload）——重组。
            CMString rframe;
            rframe.assign(rh, 9);
            rframe += rbuf;
            ChunkResendMessage rs;
            if (MessageProtocol::decode(rframe, rs) && rs.seq_ == 1) {
                const auto& c = chunks[1];
                uint64_t crc = data_checksum(c.data(), c.size());
                if (mode == FakeChunkServer::FailMode::BAD_CHUNK_STILL_BAD) crc ^= 0x01;
                CMString rf = make_chunk_frame(1, crc, c.data(), c.size());
                transport_->send_all(fd, rf.data(), rf.size());
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        transport_->close(fd);
    }

    CMSharedPtr<Transport> transport_ = create_tcp_transport();
    int listen_fd_ = -1;
    int port_ = 0;
    std::thread thread_;
};

// 测试 24：某片帧 CRC 坏 → client resend → 恢复成功。
TEST(DataTransferFakeServerTest, BadChunkResendRecovers) {
    FakeChunkServer server;
    server.mode = FakeChunkServer::FailMode::BAD_CHUNK_THEN_GOOD_RESEND;

    DataClientPool pool(1);
    auto [success, data, py_name, hash, error, rerr] =
        pool.request("127.0.0.1", server.port(), "/fake:obj", 0, 0, 5000);
    ASSERT_TRUE(success) << "error: " << error;
    ASSERT_TRUE(data && data->size() == 48);
    EXPECT_EQ(std::string(data->data(), 16), std::string(16, 'A'));
    EXPECT_EQ(std::string(data->data() + 16, 16), std::string(16, 'B'));
    EXPECT_EQ(std::string(data->data() + 32, 16), std::string(16, 'C'));
}

// 测试 25：resend 后仍坏 → CHECKSUM。
TEST(DataTransferFakeServerTest, ResendStillBadIsChecksum) {
    FakeChunkServer server;
    server.mode = FakeChunkServer::FailMode::BAD_CHUNK_STILL_BAD;

    DataClientPool pool(1);
    auto [success, data, py_name, hash, error, rerr] =
        pool.request("127.0.0.1", server.port(), "/fake:obj", 0, 0, 5000);
    EXPECT_FALSE(success);
    EXPECT_EQ(rerr, ReadError::CHECKSUM) << "error: " << error;
}

// 测试 26：DIGEST 根不匹配 → CHECKSUM。
TEST(DataTransferFakeServerTest, DigestMismatchIsChecksum) {
    FakeChunkServer server;
    server.mode = FakeChunkServer::FailMode::BAD_DIGEST;

    DataClientPool pool(1);
    auto [success, data, py_name, hash, error, rerr] =
        pool.request("127.0.0.1", server.port(), "/fake:obj", 0, 0, 5000);
    EXPECT_FALSE(success);
    EXPECT_EQ(rerr, ReadError::CHECKSUM) << "error: " << error;
}

}  // namespace fly
