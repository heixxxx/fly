#include <gtest/gtest.h>
#include <storage/cpp/data_service.h>
#include <storage/cpp/database.h>
#include <storage/cpp/data_writer.h>
#include <serialization/cpp/object_header.h>
#include <serialization/cpp/fly_buffer.h>
#include <network/cpp/data_client.h>
#include <network/cpp/data_client_pool.h>
#include <network/cpp/message_protocol.h>
#include <network/cpp/message_types.h>
#include <log/cpp/logger.h>
#include <filesystem>
#include <thread>
#include <atomic>

namespace fly {

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
    std::string db_id(32, 'b');
    ds_->register_database(db_id, test_dir_, test_dir_ + "/data");

    ds_->start_data_server("127.0.0.1", 0, 2);
    int port = ds_->get_data_port();

    DataClientPool pool(4);
    auto [success, data, py_name, hash, error] = pool.request(
        "127.0.0.1", port, db_id + ":nonexistent", 0, 0, 5000);

    EXPECT_FALSE(success);
    EXPECT_EQ(error, "OBJECT_NOT_FOUND");
}

TEST_F(DataTransferTest, DataServerReturnsDataForCompletedWrite) {
    std::string db_id(32, 'c');
    ds_->register_database(db_id, test_dir_, test_dir_ + "/data");

    std::string full = db_id + ":myobj";
    std::string test_data = "hello world test data";

    ds_->on_write_started(db_id, full);

    auto record = CMMakeShared<FlyBuffer>();
    ObjectHeader header;
    header.total_size_ = test_data.size();
    header.chunk_count_ = 1;
    header.py_name_ = "bytes";
    header.py_name_len_ = 5;
    header.compression_type_ = 0;
    CMString header_bytes = header.serialize();
    record->write(header_bytes.data(), header_bytes.size());
    record->write(test_data.data(), test_data.size());

    DataWriter writer(test_dir_, test_dir_ + "/data", "test", 0);
    writer.write_record(full, test_data.size(), 1, *record, "");
    writer.flush();

    auto entries = writer.get_all_entries(full);
    ASSERT_TRUE(entries.has_value());
    ds_->on_write_completed(db_id, full, entries.value());
    ds_->on_object_flushed(full);

    EXPECT_EQ(ds_->has_local_object(full), true);

    ds_->start_data_server("127.0.0.1", 0, 2);
    int port = ds_->get_data_port();

    DataClientPool pool(4);
    auto [success, data, py_name, hash, error] = pool.request(
        "127.0.0.1", port, full, 0, 0, 5000);

    EXPECT_TRUE(success) << "error: " << error;
    EXPECT_FALSE(data.empty());
}

TEST_F(DataTransferTest, DataClientPoolRetriesOnDataNotReady) {
    std::string db_id(32, 'd');
    ds_->register_database(db_id, test_dir_, test_dir_ + "/data");

    std::string full = db_id + ":myobj";

    ds_->on_write_started(db_id, full);

    ds_->start_data_server("127.0.0.1", 0, 2);
    int port = ds_->get_data_port();

    std::atomic<bool> write_done(false);
    std::thread writer([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        std::string test_data = "delayed data content";

        auto record = CMMakeShared<FlyBuffer>();
        ObjectHeader header;
        header.total_size_ = test_data.size();
        header.chunk_count_ = 1;
        header.py_name_ = "bytes";
        header.py_name_len_ = 5;
        header.compression_type_ = 0;
        CMString header_bytes = header.serialize();
        record->write(header_bytes.data(), header_bytes.size());
        record->write(test_data.data(), test_data.size());

        DataWriter dw(test_dir_, test_dir_ + "/data", "test2", 0);
        dw.write_record(full, test_data.size(), 1, *record, "");
        dw.flush();

        auto entries = dw.get_all_entries(full);
        if (entries.has_value()) {
            ds_->on_write_completed(db_id, full, entries.value());
        }
        ds_->on_object_flushed(full);
        write_done.store(true);
    });

    DataClientPool pool(2);
    auto [success, data, py_name, hash, error] = pool.request(
        "127.0.0.1", port, full, 0, 0, 30000);

    writer.join();

    EXPECT_TRUE(success) << "error: " << error;
    EXPECT_FALSE(data.empty());
    EXPECT_TRUE(write_done.load());
}

TEST_F(DataTransferTest, DataServerHandlesConcurrentRequestsBeyondThreadCount) {
    std::string db_id(32, 'e');
    ds_->register_database(db_id, test_dir_, test_dir_ + "/data");

    for (int i = 0; i < 6; ++i) {
        std::string name = "obj_" + std::to_string(i);
        std::string full = db_id + ":" + name;
        std::string test_data = "data for object " + std::to_string(i);

        ds_->on_write_started(db_id, full);

        auto record = CMMakeShared<FlyBuffer>();
        ObjectHeader header;
        header.total_size_ = test_data.size();
        header.chunk_count_ = 1;
        header.py_name_ = "bytes";
        header.py_name_len_ = 5;
        header.compression_type_ = 0;
        CMString header_bytes = header.serialize();
        record->write(header_bytes.data(), header_bytes.size());
        record->write(test_data.data(), test_data.size());

        DataWriter writer(test_dir_, test_dir_ + "/data", "w" + std::to_string(i), 0);
        writer.write_record(full, test_data.size(), 1, *record, "");
        writer.flush();

        auto entries = writer.get_all_entries(full);
        ASSERT_TRUE(entries.has_value());
        ds_->on_write_completed(db_id, full, entries.value());
        ds_->on_object_flushed(full);
    }

    ds_->start_data_server("127.0.0.1", 0, 2);
    int port = ds_->get_data_port();

    DataClientPool pool(4);

    std::atomic<int> success_count(0);
    CMVector<std::thread> threads;
    for (int i = 0; i < 6; ++i) {
        threads.emplace_back([&, i]() {
            auto [ok, data, py_name, hash, error] = pool.request(
                "127.0.0.1", port, db_id + ":obj_" + std::to_string(i), 0, 0, 10000);
            if (ok) success_count.fetch_add(1);
        });
    }
    for (auto& t : threads) t.join();

    EXPECT_EQ(success_count.load(), 6);
}

}  // namespace fly
