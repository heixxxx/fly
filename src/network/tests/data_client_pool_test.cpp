// Unit tests for DataClientPool DATA_NOT_READY passthrough behavior.
//
// TDD driver for the read-path hardening change: the pool must STOP internally
// polling DATA_NOT_READY and instead pass it back to the caller as a typed
// ReadError, so that TIER2 (the multi-replica + backoff layer) owns retry
// policy. Before the change these tests hang/timeout (pool loops forever on
// DATA_NOT_READY); after the change they pass (pool returns ReadError::DATA_NOT_READY).
#include <gtest/gtest.h>
#include <storage/cpp/data_service.h>
#include <serialization/cpp/object_header.h>
#include <common/cpp/fly_buffer.h>
#include <network/cpp/data_client.h>
#include <network/cpp/data_client_pool.h>
#include <network/cpp/net_quality_monitor.h>
#include <common/cpp/error_types.h>
#include <log/cpp/logger.h>
#include <filesystem>
#include <thread>
#include <chrono>
#include <future>
#include <atomic>

namespace fly {

class DataClientPoolTest : public ::testing::Test {
protected:
    CMString test_dir_;
    CMSharedPtr<DataService> ds_ = DataService::instance();

    void SetUp() override {
        test_dir_ = "/tmp/fly_test_pool_" + std::to_string(::getpid()) + "_" +
                    std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id()));
        std::filesystem::remove_all(test_dir_);
        std::filesystem::create_directories(test_dir_);
        ds_->reset();
    }

    void TearDown() override {
        ds_->stop_data_server();
        std::filesystem::remove_all(test_dir_);
    }

    // Bring up a DataServer whose state for `full` is "write in progress"
    // (on_write_started without on_write_completed), so reads return DATA_NOT_READY.
    int start_server_with_in_progress_write(const CMString& db_id, const CMString& full) {
        ds_->register_database(db_id, test_dir_, test_dir_ + "/data");
        ds_->on_write_started(db_id, full);
        ds_->start_data_server("127.0.0.1", 0, 2);
        return ds_->get_data_port();
    }
};

// After the change: pool returns ReadError::DATA_NOT_READY instead of looping.
TEST_F(DataClientPoolTest, DataNotReadyIsPassthroughNotPolled) {
    std::string db_id(fly::db_id_len(), 'd');
    std::string full = db_id + ":notready";
    int port = start_server_with_in_progress_write(db_id, full);

    DataClientPool pool(2);
    uint64_t rid = 0;

    // Run in a future with a hard wall-clock cap: before the change the pool
    // loops forever on DATA_NOT_READY, so the future would time out. After the
    // change it resolves immediately with ReadError::DATA_NOT_READY.
    auto fut = std::async(std::launch::async, [&] {
        auto [success, data, py_name, hash, error, rerr] =
            pool.request("127.0.0.1", port, full, 0, rid, 5000);
        return std::make_tuple(success, rerr);
    });

    ASSERT_EQ(fut.wait_for(std::chrono::seconds(5)), std::future_status::ready)
        << "pool.request hung on DATA_NOT_READY (still internally polling)";
    auto [success, rerr] = fut.get();

    EXPECT_FALSE(success);
    EXPECT_EQ(rerr, ReadError::DATA_NOT_READY);
}

// Non-protocol errors map to ReadError::NETWORK.
TEST_F(DataClientPoolTest, ConnectionFailureMapsToNetworkError) {
    DataClientPool pool(2);
    // Port 1 is reserved/closed on typical systems → connect failure.
    auto [success, data, py_name, hash, error, rerr] =
        pool.request("127.0.0.1", 1, "dead:beef", 0, 0, 1000);

    EXPECT_FALSE(success);
    EXPECT_EQ(rerr, ReadError::NETWORK);
}

// OBJECT_NOT_FOUND is still passed through, typed.
TEST_F(DataClientPoolTest, ObjectNotFoundIsTyped) {
    std::string db_id(fly::db_id_len(), 'e');
    ds_->register_database(db_id, test_dir_, test_dir_ + "/data");
    ds_->start_data_server("127.0.0.1", 0, 2);
    int port = ds_->get_data_port();

    DataClientPool pool(2);
    auto [success, data, py_name, hash, error, rerr] =
        pool.request("127.0.0.1", port, db_id + ":missing", 0, 0, 5000);

    EXPECT_FALSE(success);
    EXPECT_EQ(rerr, ReadError::OBJECT_NOT_FOUND);
}

// A completed exchange (even a protocol-level failure like OBJECT_NOT_FOUND)
// feeds a passive RTT sample into NetQualityMonitor, so the host becomes ranked.
TEST_F(DataClientPoolTest, CompletedExchangeFeedsPassiveRtt) {
    NetQualityMonitor::instance().clear();
    std::string db_id(fly::db_id_len(), 'f');
    ds_->register_database(db_id, test_dir_, test_dir_ + "/data");
    ds_->start_data_server("127.0.0.1", 0, 2);
    int port = ds_->get_data_port();

    DataClientPool pool(2);
    auto [success, data, py_name, hash, error, rerr] =
        pool.request("127.0.0.1", port, db_id + ":missing", 0, 0, 5000);

    ASSERT_EQ(rerr, ReadError::OBJECT_NOT_FOUND);
    // A full round-trip completed → the loopback host now has a positive score.
    EXPECT_GT(NetQualityMonitor::instance().score("127.0.0.1"), 0.0);

    NetQualityMonitor::instance().clear();
}

// A connection that never completes (no server) must NOT record a sample.
TEST_F(DataClientPoolTest, FailedConnectionFeedsNoSample) {
    NetQualityMonitor::instance().clear();
    DataClientPool pool(2);
    pool.request("127.0.0.1", 1, "dead:beef", 0, 0, 1000);  // connect fails
    EXPECT_DOUBLE_EQ(NetQualityMonitor::instance().score("127.0.0.1"), 0.0);
}

}  // namespace fly
