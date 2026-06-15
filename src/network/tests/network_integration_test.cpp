#include <gtest/gtest.h>
#include <network/cpp/connection_manager.h>
#include <network/cpp/tcp_connection_manager.h>
#include <network/cpp/message_protocol.h>
#include <network/cpp/message_types.h>
#include <network/cpp/reactor.h>
#include <thread>
#include <chrono>
#include <atomic>
#include <common/cpp/test_helpers.h>

namespace fly {
using namespace fly::test;

class NetworkIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        server_port_ = 19100 + (rand() % 1000);
    }
    
    int server_port_;
};

TEST_F(NetworkIntegrationTest, FullMessageRoundTrip) {
    TcpConnectionManager server_transport;
    TcpConnectionManager client_transport;
    
    server_transport.listen("127.0.0.1", server_port_);
    
    std::atomic<bool> server_ready{false};
    std::atomic<uint64_t> server_conn_id{0};
    std::atomic<int> messages_received{0};
    std::atomic<uint64_t> received_worker_id{0};
    
    std::thread server_thread([&] {
        auto events = server_transport.poll(2000);
        for (const auto& ev : events) {
            if (ev.type_ == TransportEventType::CONNECT) {
                server_conn_id = ev.conn_id_;
                server_ready = true;
            }
        }
        
        while (messages_received.load() < 1) {
            events = server_transport.poll(500);
            for (const auto& ev : events) {
                if (ev.type_ == TransportEventType::DATA) {
                    CMString buffer = ev.data_;
                    HeartbeatMessage msg;
                    if (MessageProtocol::decode(buffer, msg)) {
                        received_worker_id = msg.worker_id_;
                        messages_received++;
                    }
                }
            }
        }
    });
    
    uint64_t client_conn = client_transport.connect("127.0.0.1", server_port_);
    
    wait_for([&]{ return server_ready.load(); });
    
    HeartbeatMessage msg;
    msg.header_.type_ = MessageType::HEARTBEAT;
    msg.header_.message_id_ = 1;
    msg.header_.timestamp_ = std::chrono::system_clock::now().time_since_epoch().count();
    msg.worker_id_ = 12345;
    msg.running_tasks_ = {1, 2, 3};
    msg.attributes_ = {"gpu", "fast"};
    
    CMString encoded = MessageProtocol::encode(msg);
    client_transport.send(client_conn, encoded);
    
    server_thread.join();
    
    EXPECT_EQ(messages_received.load(), 1);
    EXPECT_EQ(received_worker_id.load(), 12345);
    
    server_transport.close_all();
    client_transport.close_all();
}

TEST_F(NetworkIntegrationTest, RequestResponsePattern) {
    TcpConnectionManager server_transport;
    TcpConnectionManager client_transport;
    
    server_transport.listen("127.0.0.1", server_port_ + 1);
    
    std::atomic<bool> server_ready{false};
    std::atomic<uint64_t> server_conn_id{0};
    std::atomic<bool> response_received{false};
    std::atomic<bool> data_matches{false};
    
    std::thread server_thread([&] {
        auto events = server_transport.poll(2000);
        for (const auto& ev : events) {
            if (ev.type_ == TransportEventType::CONNECT) {
                server_conn_id = ev.conn_id_;
                server_ready = true;
            }
        }
        
        bool request_received = false;
        while (!request_received) {
            events = server_transport.poll(500);
            for (const auto& ev : events) {
                if (ev.type_ == TransportEventType::DATA && !request_received) {
                    CMString buffer = ev.data_;
                    DataRequestMessage req;
                    if (MessageProtocol::decode(buffer, req)) {
                        request_received = true;
                        
                        DataResponseMessage resp;
                        resp.header_.type_ = MessageType::DATA_RESPONSE;
                        resp.header_.message_id_ = 2;
                        resp.object_name_ = req.object_name_;
                        resp.compressed_data_ = "response_data_payload";
                        
                        CMString encoded_resp = MessageProtocol::encode(resp);
                        server_transport.send(server_conn_id.load(), encoded_resp);
                    }
                }
            }
        }
    });
    
    std::thread client_receiver([&] {
        while (!response_received.load()) {
            auto events = client_transport.poll(500);
            for (const auto& ev : events) {
                if (ev.type_ == TransportEventType::DATA) {
                    CMString buffer = ev.data_;
                    DataResponseMessage resp;
                    if (MessageProtocol::decode(buffer, resp)) {
                        if (resp.compressed_data_ == "response_data_payload") {
                            data_matches = true;
                        }
                        response_received = true;
                    }
                }
            }
        }
    });
    
    uint64_t client_conn = client_transport.connect("127.0.0.1", server_port_ + 1);
    
    wait_for([&]{ return server_ready.load(); });
    
    DataRequestMessage req;
    req.header_.type_ = MessageType::DATA_REQUEST;
    req.header_.message_id_ = 1;
    req.object_name_ = "test/object";
    req.requesting_worker_id_ = 100;
    
    CMString encoded_req = MessageProtocol::encode(req);
    auto send_result = client_transport.send(client_conn, encoded_req);
    
    server_thread.join();
    client_receiver.join();
    
    EXPECT_TRUE(response_received.load());
    EXPECT_TRUE(data_matches.load());
    
    server_transport.close_all();
    client_transport.close_all();
}

TEST_F(NetworkIntegrationTest, MultipleMessagesInSequence) {
    TcpConnectionManager server_transport;
    TcpConnectionManager client_transport;
    
    server_transport.listen("127.0.0.1", server_port_ + 2);
    
    std::atomic<bool> server_ready{false};
    std::atomic<uint64_t> server_conn_id{0};
    std::atomic<int> total_received{0};
    CMVector<uint64_t> received_worker_ids;
    std::mutex ids_mutex;
    
    std::thread server_thread([&] {
        auto events = server_transport.poll(2000);
        for (const auto& ev : events) {
            if (ev.type_ == TransportEventType::CONNECT) {
                server_conn_id = ev.conn_id_;
                server_ready = true;
            }
        }
        
        CMString accumulated_buffer;
        
        while (total_received.load() < 5) {
            events = server_transport.poll(200);
            for (const auto& ev : events) {
                if (ev.type_ == TransportEventType::DATA) {
                    accumulated_buffer += ev.data_;
                    
                    while (!accumulated_buffer.empty()) {
                        HeartbeatMessage msg;
                        CMString temp = accumulated_buffer;
                        if (MessageProtocol::decode(accumulated_buffer, msg)) {
                            total_received++;
                            {
                                std::lock_guard<std::mutex> lock(ids_mutex);
                                received_worker_ids.push_back(msg.worker_id_);
                            }
                        } else {
                            accumulated_buffer = temp;
                            break;
                        }
                    }
                }
            }
        }
    });
    
    uint64_t client_conn = client_transport.connect("127.0.0.1", server_port_ + 2);
    
    wait_for([&]{ return server_ready.load(); });
    
    CMString combined_payload;
    for (int i = 0; i < 5; i++) {
        HeartbeatMessage msg;
        msg.header_.type_ = MessageType::HEARTBEAT;
        msg.header_.message_id_ = i + 1;
        msg.worker_id_ = 100 + i;
        
        CMString encoded = MessageProtocol::encode(msg);
        combined_payload += encoded;
    }
    
    client_transport.send(client_conn, combined_payload);
    
    server_thread.join();
    
    EXPECT_EQ(total_received.load(), 5);
    EXPECT_EQ(received_worker_ids.size(), 5);
    
    for (int i = 0; i < 5; i++) {
        EXPECT_EQ(received_worker_ids[i], 100 + i);
    }
    
    server_transport.close_all();
    client_transport.close_all();
}

TEST_F(NetworkIntegrationTest, LargeDataTransfer) {
    TcpConnectionManager server_transport;
    TcpConnectionManager client_transport;
    
    server_transport.listen("127.0.0.1", server_port_ + 3);
    
    std::atomic<bool> server_ready{false};
    std::atomic<uint64_t> server_conn_id{0};
    std::atomic<bool> large_data_received{false};
    std::atomic<size_t> received_size{0};
    
    std::thread server_thread([&] {
        auto events = server_transport.poll(2000);
        for (const auto& ev : events) {
            if (ev.type_ == TransportEventType::CONNECT) {
                server_conn_id = ev.conn_id_;
                server_ready = true;
            }
        }
        
        CMString accumulated_buffer;
        
        while (!large_data_received.load()) {
            events = server_transport.poll(200);
            for (const auto& ev : events) {
                if (ev.type_ == TransportEventType::DATA) {
                    accumulated_buffer += ev.data_;
                    
                    if (accumulated_buffer.size() >= 4) {
                        uint32_t expected_size = MessageProtocol::get_payload_size(accumulated_buffer);
                        if (accumulated_buffer.size() >= 4 + expected_size) {
                            CMString temp = accumulated_buffer;
                            DataResponseMessage msg;
                            if (MessageProtocol::decode(accumulated_buffer, msg)) {
                                received_size = msg.compressed_data_.size();
                                large_data_received = true;
                            }
                        }
                    }
                }
            }
        }
    });
    
    uint64_t client_conn = client_transport.connect("127.0.0.1", server_port_ + 3);
    
    wait_for([&]{ return server_ready.load(); });
    
    DataResponseMessage large_msg;
    large_msg.header_.type_ = MessageType::DATA_RESPONSE;
    large_msg.header_.message_id_ = 1;
    large_msg.object_name_ = "large_data.bin";
    large_msg.compressed_data_ = CMString(10000, 'X');
    
    CMString encoded = MessageProtocol::encode(large_msg);
    client_transport.send(client_conn, encoded);
    
    server_thread.join();
    
    EXPECT_TRUE(large_data_received.load());
    EXPECT_GE(received_size.load(), 10000);
    
    server_transport.close_all();
    client_transport.close_all();
}

TEST_F(NetworkIntegrationTest, ReactorBasedMessageHandling) {
    TcpConnectionManager server_raw;
    server_raw.listen("127.0.0.1", server_port_ + 5);
    
    std::atomic<bool> heartbeat_received{false};
    std::atomic<uint64_t> received_worker_id{0};
    std::atomic<bool> connection_established{false};
    
    auto server_transport = CMMakeUnique<TcpConnectionManager>();
    server_transport->listen("127.0.0.1", server_port_ + 6);
    
    Reactor server_reactor(std::move(server_transport));
    
    server_reactor.on_connect([&](uint64_t conn_id) {
        connection_established = true;
    });
    
    server_reactor.register_handler<HeartbeatMessage>([&](uint64_t conn_id, const HeartbeatMessage& msg) {
        received_worker_id = msg.worker_id_;
        heartbeat_received = true;
    });
    
    TcpConnectionManager client_transport;
    uint64_t client_conn = client_transport.connect("127.0.0.1", server_port_ + 6);

    while (!connection_established.load()) {
        server_reactor.run_once(100);
    }

    ASSERT_TRUE(connection_established.load()) << "Connection should be established";

    HeartbeatMessage msg;
    msg.header_.type_ = MessageType::HEARTBEAT;
    msg.header_.message_id_ = 1;
    msg.header_.timestamp_ = 12345;
    msg.worker_id_ = 88888;
    msg.running_tasks_ = {1, 2};
    msg.attributes_ = {"test"};

    CMString encoded = MessageProtocol::encode(msg);
    client_transport.send(client_conn, encoded);

    while (!heartbeat_received.load()) {
        server_reactor.run_once(100);
    }

    EXPECT_TRUE(heartbeat_received.load()) << "Heartbeat message should be received and decoded";
    EXPECT_EQ(received_worker_id.load(), 88888);
    
    server_raw.close_all();
    client_transport.close_all();
}

TEST_F(NetworkIntegrationTest, InvalidMessageHandling) {
    TcpConnectionManager server_raw;
    server_raw.listen("127.0.0.1", server_port_ + 7);
    
    std::atomic<bool> connection_established{false};
    std::atomic<int> messages_processed{0};
    
    auto server_transport = CMMakeUnique<TcpConnectionManager>();
    server_transport->listen("127.0.0.1", server_port_ + 8);
    
    Reactor server_reactor(std::move(server_transport));
    
    server_reactor.on_connect([&](uint64_t conn_id) {
        connection_established = true;
    });
    
    server_reactor.register_handler<HeartbeatMessage>([&](uint64_t conn_id, const HeartbeatMessage& msg) {
        messages_processed++;
    });
    
    TcpConnectionManager client_transport;
    uint64_t client_conn = client_transport.connect("127.0.0.1", server_port_ + 8);

    while (!connection_established.load()) {
        server_reactor.run_once(100);
    }

    ASSERT_TRUE(connection_established.load());

    CMString invalid_data = "garbage_data_not_a_valid_message";
    uint32_t len = 1 + invalid_data.size();
    CMString fake_frame;
    fake_frame.resize(4 + 1 + invalid_data.size());
    fake_frame[0] = static_cast<char>((len >> 24) & 0xFF);
    fake_frame[1] = static_cast<char>((len >> 16) & 0xFF);
    fake_frame[2] = static_cast<char>((len >> 8) & 0xFF);
    fake_frame[3] = static_cast<char>(len & 0xFF);
    fake_frame[4] = static_cast<char>(static_cast<uint8_t>(MessageType::HEARTBEAT));
    std::copy(invalid_data.begin(), invalid_data.end(), fake_frame.begin() + 5);

    client_transport.send(client_conn, fake_frame);

    server_reactor.run_once(100);

    EXPECT_EQ(messages_processed.load(), 0);
    
    server_raw.close_all();
    client_transport.close_all();
}

}  // namespace fly