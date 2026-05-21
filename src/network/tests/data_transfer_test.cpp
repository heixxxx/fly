#include <gtest/gtest.h>
#include <network/cpp/reactor.h>
#include <network/cpp/transport.h>
#include <network/cpp/message_types.h>
#include <network/cpp/data_client.h>
#include <storage/cpp/data_service.h>
#include <storage/cpp/database.h>
#include <log/cpp/logger.h>
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>
#include <mutex>
#include <cstdio>
#include <filesystem>

// Debug logging macro for deterministic state evidence in tests
#define TEST_LOG(fmt, ...) fprintf(stderr, "[TEST_DEBUG] %s:%d " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)

namespace fly {

// --- Test fixture ---

class DataTransferTest : public ::testing::Test {
protected:
    CMString test_dir_;
    DataService& ds_ = DataService::instance();

    void SetUp() override {
        test_dir_ = "/tmp/fly_test_transfer_" + std::to_string(::getpid()) + "_" +
                    std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id()));
        std::filesystem::create_directories(test_dir_);
        Logger::shutdown();
        Logger::init("test_logs/", 0);
    }

    void TearDown() override {
        ds_.stop_transfer_server();
        Logger::shutdown();
        std::filesystem::remove_all(test_dir_);
    }

    // Helper: write N objects and return their names
    CMVector<CMString> write_objects(int count, const CMString& prefix = "obj") {
        CMVector<CMString> names;
        CMString db_path = test_dir_ + "/transfer_db";
        Database db(db_path);
        for (int i = 0; i < count; i++) {
            CMString name = prefix + "/" + std::to_string(i);
            CMString data = "data_payload_" + std::to_string(i);
            db.write_object(name, data, false);
            names.push_back(db.get_obj_name(name));
        }
        TEST_LOG("Wrote %d objects to %s", count, db_path.c_str());
        return names;
    }

    // Helper: run a Reactor for a limited number of iterations (processes completions)
    void run_reactor_n(Reactor& reactor, int iterations, int timeout_ms = 50) {
        for (int i = 0; i < iterations; i++) {
            reactor.run_once(timeout_ms);
        }
    }
};

// --- Test 1: DataService submit_transfer basic correctness ---

TEST_F(DataTransferTest, SubmitTransferSingleObject) {
    auto names = write_objects(1);
    ASSERT_FALSE(names.empty());

    CMVector<TransferResult> results;
    std::mutex results_mutex;

    ds_.start_transfer_server(1, [&](const TransferResult& r) {
        std::lock_guard<std::mutex> lock(results_mutex);
        results.push_back(r);
        TEST_LOG("Completion: conn_id=%lu object=%s success=%d data_size=%zu",
                 r.conn_id, r.object_name.c_str(), r.success, r.data.size());
    });

    TEST_LOG("Submitting transfer for %s", names[0].c_str());
    ds_.submit_transfer(42, names[0]);

    // Wait for pool thread to process
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Drain completions
    auto pool = ds_.get_transfer_pool();
    ASSERT_TRUE(pool);
    pool->process_completions();

    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].conn_id, 42u);
    EXPECT_EQ(results[0].object_name, names[0]);
    EXPECT_TRUE(results[0].success);
    EXPECT_EQ(results[0].data, "data_payload_0");

    TEST_LOG("PASS: single transfer completed correctly");
}

// --- Test 2: DataService submit_transfer multiple sequential ---

TEST_F(DataTransferTest, SubmitTransferMultipleObjects) {
    int count = 5;
    auto names = write_objects(count);

    CMVector<TransferResult> results;
    std::mutex results_mutex;

    ds_.start_transfer_server(1, [&](const TransferResult& r) {
        std::lock_guard<std::mutex> lock(results_mutex);
        results.push_back(r);
        TEST_LOG("Completion: conn_id=%lu object=%s success=%d",
                 r.conn_id, r.object_name.c_str(), r.success);
    });

    for (int i = 0; i < count; i++) {
        ds_.submit_transfer(100 + i, names[i]);
    }
    TEST_LOG("Submitted %d transfers", count);

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    auto pool = ds_.get_transfer_pool();
    pool->process_completions();

    EXPECT_EQ(results.size(), static_cast<size_t>(count));
    for (const auto& r : results) {
        EXPECT_TRUE(r.success) << "Object " << r.object_name << " should succeed";
        EXPECT_FALSE(r.data.empty()) << "Object " << r.object_name << " should have data";
    }

    TEST_LOG("PASS: %zu/%d transfers completed", results.size(), count);
}

// --- Test 3: DataService submit_transfer concurrent from multiple threads ---

TEST_F(DataTransferTest, ConcurrentSubmitTransfer) {
    int object_count = 10;
    int thread_count = 4;
    auto names = write_objects(object_count);

    CMVector<TransferResult> results;
    std::mutex results_mutex;
    std::atomic<int> completion_count{0};

    ds_.start_transfer_server(2, [&](const TransferResult& r) {
        std::lock_guard<std::mutex> lock(results_mutex);
        results.push_back(r);
        completion_count++;
        TEST_LOG("[COMPLETION] thread=%lu conn_id=%lu object=%s success=%d",
                 std::hash<std::thread::id>{}(std::this_thread::get_id()),
                 r.conn_id, r.object_name.c_str(), r.success);
    });

    // Each thread submits transfers for a subset of objects
    std::vector<std::thread> threads;
    std::atomic<int> submit_count{0};

    for (int t = 0; t < thread_count; t++) {
        threads.emplace_back([&, t]() {
            for (int i = t; i < object_count; i += thread_count) {
                ds_.submit_transfer(1000 + t * 100 + i, names[i]);
                submit_count++;
                TEST_LOG("[SUBMIT] thread=%d object=%s", t, names[i].c_str());
            }
        });
    }

    for (auto& th : threads) th.join();
    TEST_LOG("All %d submits done from %d threads", submit_count.load(), thread_count);

    // Wait for pool to process all
    for (int wait = 0; wait < 50 && completion_count.load() < object_count; wait++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    auto pool = ds_.get_transfer_pool();
    pool->process_completions();

    TEST_LOG("Completions: %d/%d", completion_count.load(), object_count);

    EXPECT_EQ(results.size(), static_cast<size_t>(object_count));
    for (const auto& r : results) {
        EXPECT_TRUE(r.success) << "Object " << r.object_name << " transfer should succeed";
    }

    TEST_LOG("PASS: concurrent submit_transfer completed");
}

// --- Test 4: DataService submit_transfer for nonexistent object ---

TEST_F(DataTransferTest, SubmitTransferNonexistentObject) {
    CMVector<TransferResult> results;
    std::mutex results_mutex;

    ds_.start_transfer_server(1, [&](const TransferResult& r) {
        std::lock_guard<std::mutex> lock(results_mutex);
        results.push_back(r);
    });

    ds_.submit_transfer(1, "nonexistent/object");

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    auto pool = ds_.get_transfer_pool();
    pool->process_completions();

    ASSERT_EQ(results.size(), 1u);
    EXPECT_FALSE(results[0].success);
    EXPECT_FALSE(results[0].error_message.empty());
    TEST_LOG("PASS: nonexistent object returns success=false, error=%s", results[0].error_message.c_str());
}

// --- Test 5: try_read_local thread safety (concurrent reads) ---

TEST_F(DataTransferTest, ConcurrentTryReadLocal) {
    int object_count = 20;
    int thread_count = 8;
    // Use unique prefix to avoid conflicts with other tests sharing DataService singleton
    CMString db_path = test_dir_ + "/concurrent_read_db";
    Database db(db_path);
    CMVector<CMString> names;
    for (int i = 0; i < object_count; i++) {
        CMString name = "cread/obj_" + std::to_string(i);
        db.write_object(name, "cread_data_" + std::to_string(i), false);
        names.push_back(db.get_obj_name(name));
    }
    TEST_LOG("Wrote %d objects for concurrent read test", object_count);

    fly::DataService::instance().drain_write_back();

    std::atomic<int> success_count{0};
    std::atomic<int> fail_count{0};
    std::vector<std::thread> threads;

    for (int t = 0; t < thread_count; t++) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < object_count; i++) {
                auto [found, result] = ds_.try_read_local(names[i]);
                if (found) {
                    CMString data(result.data_buffer.begin(), result.data_buffer.end());
                    std::string expected = "cread_data_" + std::to_string(i);
                    if (data == expected) {
                        success_count++;
                    } else {
                        TEST_LOG("[THREAD %d] DATA MISMATCH for %s: got '%s' expected '%s'",
                                 t, names[i].c_str(), data.c_str(), expected.c_str());
                        fail_count++;
                    }
                } else {
                    TEST_LOG("[THREAD %d] NOT FOUND: %s", t, names[i].c_str());
                    fail_count++;
                }
            }
        });
    }

    for (auto& th : threads) th.join();

    int expected_total = thread_count * object_count;
    TEST_LOG("Concurrent try_read_local: %d/%d success, %d fail",
             success_count.load(), expected_total, fail_count.load());

    EXPECT_EQ(success_count.load(), expected_total);
    EXPECT_EQ(fail_count.load(), 0);
}

// --- Test 6: DataClient to Reactor+DataService end-to-end ---

TEST_F(DataTransferTest, DataClientToReactorSingleRequest) {
    // Setup: write data, start DataService transfer server, start Reactor
    auto names = write_objects(1, "e2e");
    ASSERT_FALSE(names.empty());

    // Create Reactor with a listening server
    auto server_transport = create_transport("tcp");
    server_transport->listen("127.0.0.1", 0);
    int server_port = server_transport->get_bound_port();
    TEST_LOG("Server listening on port %d", server_port);

    auto reactor = CMMakeUnique<Reactor>(std::move(server_transport));

    // Register DataRequestMessage handler — mimics WorkerAgent::on_data_request
    reactor->register_handler<DataRequestMessage>(
        [&](uint64_t conn_id, const DataRequestMessage& msg) {
            TEST_LOG("Reactor received DataRequest for %s", msg.object_name.c_str());
            ds_.submit_transfer(conn_id, msg.object_name);
        });

    // Start DataService with completion callback that sends response via Reactor
    ds_.start_transfer_server(1, [&](const TransferResult& r) {
        TEST_LOG("Transfer completion: object=%s success=%d", r.object_name.c_str(), r.success);
        DataResponseMessage response;
        response.object_name = r.object_name;
        response.success = r.success;
        response.data = r.data;
        if (!r.success) response.error_message = r.error_message;
        reactor->send(r.conn_id, response);
    });

    reactor->set_io_pool(ds_.get_transfer_pool());

    // Run Reactor in background thread (use run() which calls process_completions internally)
    std::atomic<bool> reactor_running{true};
    std::thread reactor_thread([&]() {
        while (reactor_running.load()) {
            reactor->run_once(50);
            // Must manually drain completions since we use run_once, not run()
            auto pool = ds_.get_transfer_pool();
            if (pool) pool->process_completions();
        }
    });

    // Give reactor time to start
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Client: use DataClient to request data
    TEST_LOG("Client requesting %s from 127.0.0.1:%d", names[0].c_str(), server_port);
    auto [success, data, error] = DataClient::request_data("127.0.0.1", server_port, names[0], 3000);

    EXPECT_TRUE(success) << "DataClient request should succeed, error: " << error;
    EXPECT_EQ(data, "data_payload_0");
    TEST_LOG("Client received data: '%s'", data.c_str());

    reactor_running = false;
    reactor_thread.join();

    TEST_LOG("PASS: DataClient to Reactor+DataService single request");
}

// --- Test 7: DataClient concurrent requests to same server ---

TEST_F(DataTransferTest, DataClientConcurrentRequests) {
    int object_count = 8;
    int client_count = 4;
    auto names = write_objects(object_count, "conc");

    auto server_transport = create_transport("tcp");
    server_transport->listen("127.0.0.1", 0);
    int server_port = server_transport->get_bound_port();
    TEST_LOG("Server listening on port %d for concurrent test", server_port);

    auto reactor = CMMakeUnique<Reactor>(std::move(server_transport));

    reactor->register_handler<DataRequestMessage>(
        [&](uint64_t conn_id, const DataRequestMessage& msg) {
            ds_.submit_transfer(conn_id, msg.object_name);
        });

    ds_.start_transfer_server(2, [&](const TransferResult& r) {
        DataResponseMessage response;
        response.object_name = r.object_name;
        response.success = r.success;
        response.data = r.data;
        if (!r.success) response.error_message = r.error_message;
        reactor->send(r.conn_id, response);
    });

    reactor->set_io_pool(ds_.get_transfer_pool());

    std::atomic<bool> reactor_running{true};
    std::thread reactor_thread([&]() {
        while (reactor_running.load()) {
            reactor->run_once(50);
            auto pool = ds_.get_transfer_pool();
            if (pool) pool->process_completions();
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Launch client_count threads, each requesting object_count/client_count objects
    std::atomic<int> success_count{0};
    std::atomic<int> fail_count{0};
    std::vector<std::thread> client_threads;

    for (int t = 0; t < client_count; t++) {
        client_threads.emplace_back([&, t]() {
            for (int i = t; i < object_count; i += client_count) {
                TEST_LOG("[CLIENT %d] requesting %s", t, names[i].c_str());
                auto [ok, data, err] = DataClient::request_data(
                    "127.0.0.1", server_port, names[i], 5000);

                if (ok) {
                    std::string expected = "data_payload_" + std::to_string(i);
                    if (data == expected) {
                        success_count++;
                        TEST_LOG("[CLIENT %d] SUCCESS for %s data='%s'", t, names[i].c_str(), data.c_str());
                    } else {
                        fail_count++;
                        TEST_LOG("[CLIENT %d] DATA MISMATCH for %s: got '%s' expected '%s'",
                                 t, names[i].c_str(), data.c_str(), expected.c_str());
                    }
                } else {
                    fail_count++;
                    TEST_LOG("[CLIENT %d] FAILED for %s: %s", t, names[i].c_str(), err.c_str());
                }
            }
        });
    }

    for (auto& th : client_threads) th.join();

    TEST_LOG("Concurrent clients done: %d success, %d fail", success_count.load(), fail_count.load());

    reactor_running = false;
    reactor_thread.join();

    EXPECT_EQ(success_count.load(), object_count);
    EXPECT_EQ(fail_count.load(), 0);
}

// --- Test 8: DataClient connection failure (timeout) ---

TEST_F(DataTransferTest, DataClientConnectionFailure) {
    auto [success, data, error] = DataClient::request_data(
        "127.0.0.1", 19999, "nonexistent", 500);

    EXPECT_FALSE(success);
    EXPECT_FALSE(error.empty());
    TEST_LOG("PASS: connection failure handled, error=%s", error.c_str());
}

// --- Test 9: DataService stop_transfer_server drains correctly ---

TEST_F(DataTransferTest, StopTransferServer) {
    auto names = write_objects(1);

    CMVector<TransferResult> results;
    std::mutex results_mutex;

    ds_.start_transfer_server(1, [&](const TransferResult& r) {
        std::lock_guard<std::mutex> lock(results_mutex);
        results.push_back(r);
    });

    EXPECT_TRUE(ds_.is_transfer_server_running());

    ds_.submit_transfer(1, names[0]);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    auto pool = ds_.get_transfer_pool();
    pool->process_completions();
    EXPECT_GE(results.size(), 1u);

    ds_.stop_transfer_server();
    EXPECT_FALSE(ds_.is_transfer_server_running());

    // After stop, submit should be no-op
    results.clear();
    ds_.submit_transfer(2, names[0]);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(results.empty());

    TEST_LOG("PASS: stop_transfer_server works correctly");
}

// --- Test 10: Mixed concurrent read + write stress ---

TEST_F(DataTransferTest, ConcurrentReadWhileWriting) {
    int write_count = 20;
    int reader_count = 4;
    CMString db_path = test_dir_ + "/stress_db";

    // Pre-write some objects
    CMVector<CMString> pre_names;
    {
        Database db(db_path);
        for (int i = 0; i < write_count / 2; i++) {
            CMString name = "stress/pre_" + std::to_string(i);
            db.write_object(name, "pre_data_" + std::to_string(i), false);
            pre_names.push_back(db.get_obj_name(name));
        }
    }
    TEST_LOG("Pre-wrote %zu objects", pre_names.size());

    // Concurrent readers + writer
    std::atomic<int> read_success{0};
    std::atomic<int> read_not_found{0};
    std::atomic<int> read_mismatch{0};
    std::vector<std::thread> threads;

    // Writer thread: writes more objects
    threads.emplace_back([&]() {
        Database db2(db_path + "_writer");
        for (int i = write_count / 2; i < write_count; i++) {
            CMString name = "stress/post_" + std::to_string(i);
            db2.write_object(name, "post_data_" + std::to_string(i), false);
            TEST_LOG("[WRITER] wrote %s", name.c_str());
        }
    });

    // Reader threads: read pre-written objects
    for (int t = 0; t < reader_count; t++) {
        threads.emplace_back([&, t]() {
            for (size_t i = 0; i < pre_names.size(); i++) {
                auto [found, result] = ds_.try_read_local(pre_names[i]);
                if (found) {
                    CMString data(result.data_buffer.begin(), result.data_buffer.end());
                    std::string expected = "pre_data_" + std::to_string(i);
                    if (data == expected) {
                        read_success++;
                    } else {
                        read_mismatch++;
                        TEST_LOG("[READER %d] MISMATCH %s", t, pre_names[i].c_str());
                    }
                } else {
                    read_not_found++;
                }
            }
        });
    }

    for (auto& th : threads) th.join();

    int expected_reads = reader_count * static_cast<int>(pre_names.size());
    TEST_LOG("Stress test: %d/%d reads ok, %d not_found, %d mismatch",
             read_success.load(), expected_reads, read_not_found.load(), read_mismatch.load());

    EXPECT_EQ(read_success.load(), expected_reads);
    EXPECT_EQ(read_mismatch.load(), 0);
}

}  // namespace fly
