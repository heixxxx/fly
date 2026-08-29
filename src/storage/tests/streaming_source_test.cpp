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
#include <serialization/cpp/object_header.h>
#include <common/cpp/fly_buffer.h>
#include <common/cpp/data_checksum.h>
#include <core/cpp/config.h>
#include <log/cpp/logger.h>
#include <filesystem>
#include <thread>
#include <chrono>
#include <cstring>

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
        test_dir_ = "/tmp/fly_test_stream_" + std::to_string(::getpid()) + "_" +
                    std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id()));
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

}  // namespace fly
