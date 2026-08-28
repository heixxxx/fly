// 零容忍数据校验语义测试（chunked-transfer-design.md §5 / 测试 10-12）。
//
// 语义锚定：
//   校验类错误（帧头 check / wire 根 CRC / 磁盘块 CRC / trailer）→
//   [FATAL-DATA-CORRUPTION] ERR → 失效缓存 → 【一次】对象级重取（远程换副本
//   优先 / 仅本地则绕过缓存重读盘）→ 仍败（任何方式：仍校验失败/断连/超时/
//   无数据）→ DataCorruptionError 上抛（Python 面转 RuntimeError FATAL →
//   TaskFailed；worker 不崩溃）。
//   不做静默重试循环——持续校验失败 = 内存/硬件/代码缺陷，必须大声暴露。
#include <gtest/gtest.h>
#include <storage/cpp/data_service.h>
#include <storage/cpp/database.h>
#include <storage/cpp/data_writer.h>
#include <storage/cpp/decompressing_streambuf.h>
#include <serialization/cpp/object_header.h>
#include <common/cpp/fly_buffer.h>
#include <common/cpp/data_checksum.h>
#include <common/cpp/error_types.h>
#include <log/cpp/logger.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

namespace fly {
namespace {

// 组装合法新格式 record（单 raw 块 + trailer）。
FlyBufferPtr make_valid_record(const std::string& data, const CMString& py_name) {
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
    header.compression_type_ = 0;
    CMString trailer = header.serialize_trailer();
    record->write(trailer.data(), trailer.size());
    return record;
}

}  // namespace

class DataCorruptionTest : public ::testing::Test {
protected:
    CMString test_dir_;
    CMSharedPtr<DataService> ds_ = DataService::instance();

    void SetUp() override {
        test_dir_ = "/tmp/fly_test_corrupt_" + std::to_string(::getpid()) + "_" +
                    std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id()));
        std::filesystem::remove_all(test_dir_);
        std::filesystem::create_directories(test_dir_);
        ds_->reset();
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
    }

    // 写一个对象并完成登记（盘上有完整合法 record）。
    void write_valid_object(const CMString& db_path, const CMString& full,
                            const std::string& data) {
        ds_->register_database(db_path, test_dir_ + "/data");
        ds_->on_write_started(db_path, full);
        DataWriter writer(test_dir_, test_dir_ + "/data", "dcw", 0);
        auto rec = make_valid_record(data, "bytes");
        writer.write_record(full, data.size(), 1, *rec, "");
        writer.flush();
        auto entries = writer.get_all_entries(full);
        ASSERT_TRUE(entries.has_value());
        ds_->on_write_completed(db_path, full, entries.value());
        ds_->on_object_flushed(full);
    }
};

// 测试 10：缓存条目损坏（旧格式/坏 trailer）→ 失效缓存重读盘一次 → 干净数据。
TEST_F(DataCorruptionTest, LocalRetryRecoversFromBadCache) {
    CMString db_path = "/dcok";
    CMString full = db_path + ":obj";
    std::string good = "good object payload";
    write_valid_object(db_path, full, good);

    // 污染 low-tier 缓存：塞一个旧格式（前置 header）坏条目。
    ObjectHeader legacy;
    legacy.total_size_ = 5;
    legacy.chunk_count_ = 1;
    legacy.py_name_ = "bytes";
    legacy.py_name_len_ = 5;
    CMString bad = legacy.serialize();
    bad += "xxxxx";
    auto bad_buf = CMMakeShared<FlyBuffer>();
    bad_buf->write(bad.data(), bad.size());
    ObjectCache::instance().put_low(full, bad_buf, bad.size());

    // Database 层读：缓存 trailer 解析失败 → 失效 → 重读源 → 干净 record。
    Database db(db_path, test_dir_ + "/data");
    auto [comp, py_name] = db.read_object_compressed("obj", false);
    ASSERT_TRUE(comp && !comp->empty());

    ObjectHeader hdr;
    size_t tl = 0;
    EXPECT_TRUE(ObjectHeader::deserialize_trailer({comp->data(), comp->size()}, hdr, tl));
    EXPECT_EQ(hdr.py_name_, "bytes");

    // 解压出口干净。
    DecompressingStreamBuf dsbuf(comp->data(), comp->size());
    std::istream is(&dsbuf);
    CMString got(good.size(), '\0');
    is.read(got.data(), good.size());
    EXPECT_EQ(got, good);
    EXPECT_FALSE(dsbuf.checksum_failed());
}

// 测试 11：盘上损坏且无远程副本 → 重读仍败 → DataCorruptionError（FATAL）。
TEST_F(DataCorruptionTest, LocalDiskCorruptFatal) {
    CMString db_path = "/dcbad";
    CMString full = db_path + ":obj";
    std::string good = "object that will be corrupted on disk";
    write_valid_object(db_path, full, good);

    // 位腐 trailer：读出 .dat 字节翻转末字节（trailer CRC 域）后原长度写回。
    // （保持文件大小不变——对象区间读取不越界，损坏精确落在 trailer 校验上。）
    std::filesystem::path data_dir(test_dir_ + "/data");
    for (const auto& entry : std::filesystem::directory_iterator(data_dir)) {
        if (entry.path().filename().string().rfind("data_dcw", 0) == 0) {
            std::ifstream in(entry.path(), std::ios::binary);
            CMString bytes((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
            in.close();
            ASSERT_GT(bytes.size(), 8u);
            bytes[bytes.size() - 1] = static_cast<char>(bytes[bytes.size() - 1] ^ 0x01);
            std::ofstream out(entry.path(), std::ios::binary | std::ios::trunc);
            out.write(bytes.data(), bytes.size());
        }
    }

    Database db(db_path, test_dir_ + "/data");
    // 盘坏 → 重取（无远程副本 → 必败）→ FATAL 异常。
    EXPECT_THROW({
        try {
            db.read_object_compressed("obj", false);
        } catch (const DataCorruptionError& e) {
            EXPECT_NE(std::string(e.what()).find("[FATAL-DATA-CORRUPTION]"), std::string::npos);
            throw;
        }
    }, DataCorruptionError);
}

// 测试 12：远程校验失败的一次重取编排（cb 注入，确定性）。
TEST_F(DataCorruptionTest, RemoteChunkCorruptRetryThenFatal) {
    // 场景 C：CHECKSUM → 干净成功 = 成功（一次重取预算内换副本命中）。
    {
        CMString full = "/dc12a:obj";
        ds_->update_remote_idx(full, 1, "host_a", 8000);
        ds_->update_remote_idx(full, 2, "host_b", 9000);

        auto good = make_valid_record("clean data", "bytes");
        ds_->set_direct_compressed_read_handler(
            [&](const CMString& host, int32_t, const CMString&)
                -> std::tuple<bool, FlyBufferPtr, CMString, CMString, ReadError> {
                if (host == "host_a") {
                    return {false, nullptr, {}, {}, ReadError::CHECKSUM};
                }
                return {true, good, "bytes", {}, ReadError::NONE};
            });

        auto [found, raw, py, hash, csp] = ds_->read_raw_compressed(full);
        EXPECT_TRUE(found);
        EXPECT_TRUE(raw && !raw->empty());
        ds_->set_direct_compressed_read_handler(nullptr);
    }

    // 场景 A：CHECKSUM → CHECKSUM = fatal（预算耗尽）。
    {
        CMString full = "/dc12b:obj";
        ds_->update_remote_idx(full, 1, "host_a", 8000);
        ds_->update_remote_idx(full, 2, "host_b", 9000);

        ds_->set_direct_compressed_read_handler(
            [](const CMString&, int32_t, const CMString&)
                -> std::tuple<bool, FlyBufferPtr, CMString, CMString, ReadError> {
                return {false, nullptr, {}, {}, ReadError::CHECKSUM};
            });

        EXPECT_THROW(ds_->read_raw_compressed(full), DataCorruptionError);
        ds_->set_direct_compressed_read_handler(nullptr);
    }

    // 场景 B：CHECKSUM → 断连 = fatal（重取模式下任何失败都不可接受，§5）。
    {
        CMString full = "/dc12c:obj";
        ds_->update_remote_idx(full, 1, "host_a", 8000);
        ds_->update_remote_idx(full, 2, "host_b", 9000);

        ds_->set_direct_compressed_read_handler(
            [&](const CMString& host, int32_t, const CMString&)
                -> std::tuple<bool, FlyBufferPtr, CMString, CMString, ReadError> {
                if (host == "host_a") {
                    return {false, nullptr, {}, {}, ReadError::CHECKSUM};
                }
                return {false, nullptr, {}, {}, ReadError::NETWORK};
            });

        EXPECT_THROW(ds_->read_raw_compressed(full), DataCorruptionError);
        ds_->set_direct_compressed_read_handler(nullptr);
    }
}

}  // namespace fly
