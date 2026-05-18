#include <gtest/gtest.h>
#include <network/cpp/transport.h>
#include <network/cpp/tcp_transport.h>
#include <thread>
#include <chrono>

namespace fly {

TEST(TransportLayerTest, CreateTCPTransport) {
    auto transport = create_transport("tcp");
    EXPECT_NE(transport, nullptr);
    EXPECT_EQ(transport->connection_count(), 0);
}

TEST(TCPTransportTest, ListenAndStop) {
    TCPTransport transport;
    EXPECT_NO_THROW(transport.listen("127.0.0.1", 19001));
    EXPECT_NO_THROW(transport.stop_listening());
}

TEST(TCPTransportTest, ListenAndConnect) {
    TCPTransport server;
    TCPTransport client;
    
    server.listen("127.0.0.1", 19002);
    
    uint64_t conn_id = client.connect("127.0.0.1", 19002);
    EXPECT_GT(conn_id, 0);
    
    auto server_events = server.poll(1000);
    EXPECT_GE(server_events.size(), 1);
    
    bool found_connect = false;
    for (const auto& ev : server_events) {
        if (ev.type == TransportEventType::CONNECT) {
            found_connect = true;
            EXPECT_GT(ev.conn_id, 0);
            break;
        }
    }
    EXPECT_TRUE(found_connect);
    
    server.close_all();
    client.close_all();
}

TEST(TCPTransportTest, SendAndRecv) {
    TCPTransport server;
    TCPTransport client;
    
    server.listen("127.0.0.1", 19003);
    uint64_t client_conn = client.connect("127.0.0.1", 19003);
    
    auto server_events = server.poll(1000);
    EXPECT_GE(server_events.size(), 1);
    
    uint64_t server_conn = 0;
    for (const auto& ev : server_events) {
        if (ev.type == TransportEventType::CONNECT) {
            server_conn = ev.conn_id;
            break;
        }
    }
    EXPECT_GT(server_conn, 0);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    CMString test_msg = "hello world";
    ssize_t sent = client.send(client_conn, test_msg);
    EXPECT_GT(sent, 0);
    
    server_events = server.poll(1000);
    bool found_data = false;
    for (const auto& ev : server_events) {
        if (ev.type == TransportEventType::DATA) {
            found_data = true;
            EXPECT_EQ(ev.data, test_msg);
            break;
        }
    }
    EXPECT_TRUE(found_data);
    
    server.close_all();
    client.close_all();
}

TEST(TCPTransportTest, ConnectionCount) {
    TCPTransport server;
    TCPTransport client;
    
    server.listen("127.0.0.1", 19004);
    
    EXPECT_EQ(server.connection_count(), 0);
    
    client.connect("127.0.0.1", 19004);
    server.poll(1000);
    
    EXPECT_EQ(server.connection_count(), 1);
    
    server.close_all();
    client.close_all();
}

TEST(TCPTransportTest, InvalidTransportType) {
    EXPECT_THROW(create_transport("udp"), std::runtime_error);
    EXPECT_THROW(create_transport("rdma"), std::runtime_error);
}

TEST(TCPTransportTest, MultipleConnections) {
    TCPTransport server;
    
    server.listen("127.0.0.1", 19005);
    
    std::vector<CMUniquePtr<TCPTransport>> clients;
    std::vector<uint64_t> client_conns;
    
    for (int i = 0; i < 5; i++) {
        clients.push_back(CMMakeUnique<TCPTransport>());
        client_conns.push_back(clients[i]->connect("127.0.0.1", 19005));
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    int connect_count = 0;
    for (int i = 0; i < 10 && connect_count < 5; i++) {
        auto server_events = server.poll(100);
        for (const auto& ev : server_events) {
            if (ev.type == TransportEventType::CONNECT) {
                connect_count++;
            }
        }
    }
    EXPECT_GE(connect_count, 5);
    
    server.close_all();
    for (auto& c : clients) {
        c->close_all();
    }
}

}  // namespace fly