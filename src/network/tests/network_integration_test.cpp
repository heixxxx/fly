#include <gtest/gtest.h>
#include <network/cpp/transport.h>
#include <network/cpp/tcp_transport.h>
#include <network/cpp/message_protocol.h>
#include <network/cpp/message_types.h>
#include <network/cpp/reactor.h>
#include <thread>
#include <chrono>
#include <atomic>

namespace fly {

class NetworkIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        server_port_ = 19100 + (rand() % 1000);
    }
    
    int server_port_;
};

TEST_F(NetworkIntegrationTest, FullMessageRoundTrip) {
    TCPTransport server_transport;
    TCPTransport client_transport;
    
    server_transport.listen("127.0.0.1", server_port_);
    
    std::atomic<bool> server_ready{false};
    std::atomic<uint64_t> server_conn_id{0};
    std::atomic<int> messages_received{0};
    std::atomic<uint64_t> received_worker_id{0};
    
    std::thread server_thread([&] {
        auto events = server_transport.poll(2000);
        for (const auto& ev : events) {
            if (ev.type == TransportEventType::CONNECT) {
                server_conn_id = ev.conn_id;
                server_ready = true;
            }
        }
        
        while (messages_received.load() < 1) {
            events = server_transport.poll(500);
            for (const auto& ev : events) {
                if (ev.type == TransportEventType::DATA) {
                    CMString buffer = ev.data;
                    HeartbeatMessage msg;
                    if (MessageProtocol::decode(buffer, msg)) {
                        received_worker_id = msg.worker_id;
                        messages_received++;
                    }
                }
            }
        }
    });
    
    uint64_t client_conn = client_transport.connect("127.0.0.1", server_port_);
    
    while (!server_ready.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    HeartbeatMessage msg;
    msg.header.type = MessageType::HEARTBEAT;
    msg.header.message_id = 1;
    msg.header.timestamp = std::chrono::system_clock::now().time_since_epoch().count();
    msg.worker_id = 12345;
    msg.running_tasks = {1, 2, 3};
    msg.attributes = {"gpu", "fast"};
    
    CMString encoded = MessageProtocol::encode(msg);
    client_transport.send(client_conn, encoded);
    
    server_thread.join();
    
    EXPECT_EQ(messages_received.load(), 1);
    EXPECT_EQ(received_worker_id.load(), 12345);
    
    server_transport.close_all();
    client_transport.close_all();
}

TEST_F(NetworkIntegrationTest, RequestResponsePattern) {
    TCPTransport server_transport;
    TCPTransport client_transport;
    
    server_transport.listen("127.0.0.1", server_port_ + 1);
    
    std::atomic<bool> server_ready{false};
    std::atomic<uint64_t> server_conn_id{0};
    std::atomic<bool> response_received{false};
    std::atomic<bool> data_matches{false};
    
    std::thread server_thread([&] {
        auto events = server_transport.poll(2000);
        for (const auto& ev : events) {
            if (ev.type == TransportEventType::CONNECT) {
                server_conn_id = ev.conn_id;
                server_ready = true;
            }
        }
        
        bool request_received = false;
        while (!request_received) {
            events = server_transport.poll(500);
            for (const auto& ev : events) {
                if (ev.type == TransportEventType::DATA && !request_received) {
                    CMString buffer = ev.data;
                    DataRequestMessage req;
                    if (MessageProtocol::decode(buffer, req)) {
                        request_received = true;
                        
                        DataResponseMessage resp;
                        resp.header.type = MessageType::DATA_RESPONSE;
                        resp.header.message_id = 2;
                        resp.object_name = req.object_name;
                        resp.data = "response_data_payload";
                        
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
                if (ev.type == TransportEventType::DATA) {
                    CMString buffer = ev.data;
                    DataResponseMessage resp;
                    if (MessageProtocol::decode(buffer, resp)) {
                        if (resp.data == "response_data_payload") {
                            data_matches = true;
                        }
                        response_received = true;
                    }
                }
            }
        }
    });
    
    uint64_t client_conn = client_transport.connect("127.0.0.1", server_port_ + 1);
    
    while (!server_ready.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    DataRequestMessage req;
    req.header.type = MessageType::DATA_REQUEST;
    req.header.message_id = 1;
    req.object_name = "test/object";
    req.requesting_worker_id = 100;
    
    CMString encoded_req = MessageProtocol::encode(req);
    client_transport.send(client_conn, encoded_req);
    
    server_thread.join();
    client_receiver.join();
    
    EXPECT_TRUE(response_received.load());
    EXPECT_TRUE(data_matches.load());
    
    server_transport.close_all();
    client_transport.close_all();
}

TEST_F(NetworkIntegrationTest, MultipleMessagesInSequence) {
    TCPTransport server_transport;
    TCPTransport client_transport;
    
    server_transport.listen("127.0.0.1", server_port_ + 2);
    
    std::atomic<bool> server_ready{false};
    std::atomic<uint64_t> server_conn_id{0};
    std::atomic<int> total_received{0};
    CMVector<uint64_t> received_worker_ids;
    std::mutex ids_mutex;
    
    std::thread server_thread([&] {
        auto events = server_transport.poll(2000);
        for (const auto& ev : events) {
            if (ev.type == TransportEventType::CONNECT) {
                server_conn_id = ev.conn_id;
                server_ready = true;
            }
        }
        
        CMString accumulated_buffer;
        
        while (total_received.load() < 5) {
            events = server_transport.poll(200);
            for (const auto& ev : events) {
                if (ev.type == TransportEventType::DATA) {
                    accumulated_buffer += ev.data;
                    
                    while (!accumulated_buffer.empty()) {
                        HeartbeatMessage msg;
                        CMString temp = accumulated_buffer;
                        if (MessageProtocol::decode(accumulated_buffer, msg)) {
                            total_received++;
                            {
                                std::lock_guard<std::mutex> lock(ids_mutex);
                                received_worker_ids.push_back(msg.worker_id);
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
    
    while (!server_ready.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    CMString combined_payload;
    for (int i = 0; i < 5; i++) {
        HeartbeatMessage msg;
        msg.header.type = MessageType::HEARTBEAT;
        msg.header.message_id = i + 1;
        msg.worker_id = 100 + i;
        
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
    TCPTransport server_transport;
    TCPTransport client_transport;
    
    server_transport.listen("127.0.0.1", server_port_ + 3);
    
    std::atomic<bool> server_ready{false};
    std::atomic<uint64_t> server_conn_id{0};
    std::atomic<bool> large_data_received{false};
    std::atomic<size_t> received_size{0};
    
    std::thread server_thread([&] {
        auto events = server_transport.poll(2000);
        for (const auto& ev : events) {
            if (ev.type == TransportEventType::CONNECT) {
                server_conn_id = ev.conn_id;
                server_ready = true;
            }
        }
        
        CMString accumulated_buffer;
        
        while (!large_data_received.load()) {
            events = server_transport.poll(200);
            for (const auto& ev : events) {
                if (ev.type == TransportEventType::DATA) {
                    accumulated_buffer += ev.data;
                    
                    if (accumulated_buffer.size() >= 4) {
                        uint32_t expected_size = MessageProtocol::get_payload_size(accumulated_buffer);
                        if (accumulated_buffer.size() >= 4 + expected_size) {
                            CMString temp = accumulated_buffer;
                            DataResponseMessage msg;
                            if (MessageProtocol::decode(accumulated_buffer, msg)) {
                                received_size = msg.data.size();
                                large_data_received = true;
                            }
                        }
                    }
                }
            }
        }
    });
    
    uint64_t client_conn = client_transport.connect("127.0.0.1", server_port_ + 3);
    
    while (!server_ready.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    DataResponseMessage large_msg;
    large_msg.header.type = MessageType::DATA_RESPONSE;
    large_msg.header.message_id = 1;
    large_msg.object_name = "large_data.bin";
    large_msg.data = CMString(10000, 'X');
    
    CMString encoded = MessageProtocol::encode(large_msg);
    client_transport.send(client_conn, encoded);
    
    server_thread.join();
    
    EXPECT_TRUE(large_data_received.load());
    EXPECT_GE(received_size.load(), 10000);
    
    server_transport.close_all();
    client_transport.close_all();
}

TEST_F(NetworkIntegrationTest, ReactorBasedMessageHandling) {
    TCPTransport server_transport;
    server_transport.listen("127.0.0.1", server_port_ + 5);
    
    TCPTransport client_transport;
    client_transport.connect("127.0.0.1", server_port_ + 5);
    
    auto events = server_transport.poll(500);
    
    bool found_connect = false;
    for (const auto& ev : events) {
        if (ev.type == TransportEventType::CONNECT) {
            found_connect = true;
        }
    }
    
    EXPECT_TRUE(found_connect);
    
    server_transport.close_all();
    client_transport.close_all();
}

}  // namespace fly