#include <gtest/gtest.h>
#include <storage/cpp/data_service.h>
#include <storage/cpp/database.h>
#include <storage/cpp/data_writer.h>
#include <serialization/cpp/object_header.h>
#include <common/cpp/fly_buffer.h>
#include <network/cpp/data_client.h>
#include <network/cpp/data_client_pool.h>
#include <network/cpp/tcp_socket.h>
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
    std::string db_id(fly::db_id_len(), 'b');
    ds_->register_database(db_id, test_dir_, test_dir_ + "/data");

    ds_->start_data_server("127.0.0.1", 0, 2);
    int port = ds_->get_data_port();

    DataClientPool pool(4);
    auto [success, data, py_name, hash, error, rerr] = pool.request(
        "127.0.0.1", port, db_id + ":nonexistent", 0, 0, 5000);

    EXPECT_FALSE(success);
    EXPECT_EQ(error, "OBJECT_NOT_FOUND");
    EXPECT_EQ(rerr, ReadError::OBJECT_NOT_FOUND);
}

TEST_F(DataTransferTest, DataServerReturnsDataForCompletedWrite) {
    std::string db_id(fly::db_id_len(), 'c');
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
    auto [success, data, py_name, hash, error, rerr] = pool.request(
        "127.0.0.1", port, full, 0, 0, 5000);

    EXPECT_TRUE(success) << "error: " << error;
    EXPECT_FALSE(!data || data->empty());
}

// After the read-path hardening change, the pool does NOT internally retry
// DATA_NOT_READY — it returns immediately so the TIER2 layer owns retry policy.
TEST_F(DataTransferTest, DataClientPoolReturnsDataNotReadyImmediately) {
    std::string db_id(fly::db_id_len(), 'd');
    ds_->register_database(db_id, test_dir_, test_dir_ + "/data");

    std::string full = db_id + ":myobj";

    ds_->on_write_started(db_id, full);

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
    std::string db_id(fly::db_id_len(), 'e');
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
            auto [ok, data, py_name, hash, error, rerr] = pool.request(
                "127.0.0.1", port, db_id + ":obj_" + std::to_string(i), 0, 0, 10000);
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

    // Read 5B frame header [4B total_len][1B type].
    char hdr[5];
    size_t got = 0;
    while (got < 5) {
        ssize_t n = transport->recv(fd, hdr + got, 5 - got);
        ASSERT_GT(n, 0);
        got += static_cast<size_t>(n);
    }
    uint32_t total_len = read_be32(hdr);
    ASSERT_EQ(static_cast<uint8_t>(hdr[4]),
              static_cast<uint8_t>(MessageType::NET_PROBE_RESPONSE));

    // total_len = 1(type, already read) + payload_len. Read the remaining
    // payload bytes, then reassemble the full frame for decode.
    uint32_t payload_len = total_len - 1;
    CMString rest(payload_len, '\0');
    size_t rgot = 0;
    while (rgot < payload_len) {
        ssize_t n = transport->recv(fd, rest.data() + rgot, payload_len - rgot);
        ASSERT_GT(n, 0) << "recv rest failed at offset " << rgot << "/" << payload_len;
        rgot += static_cast<size_t>(n);
    }
    CMString frame;
    frame.assign(hdr, 5);
    frame += rest;

    NetProbeResponseMessage resp;
    ASSERT_TRUE(MessageProtocol::decode(frame, resp));
    EXPECT_EQ(resp.probe_seq_, 42u);
    EXPECT_EQ(resp.payload_.size(), 8192u);

    transport->close(fd);
}

}  // namespace fly
