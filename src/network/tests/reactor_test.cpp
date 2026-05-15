#include <gtest/gtest.h>
#include <network/cpp/reactor.h>
#include <network/cpp/tcp_transport.h>
#include <network/cpp/message_types.h>
#include <thread>
#include <chrono>
#include <atomic>

namespace fly {

TEST(ReactorTest, CreateReactor) {
    auto transport = create_transport("tcp");
    Reactor reactor(std::move(transport));
    
    EXPECT_NO_THROW(reactor.run_once(10));
}

TEST(ReactorTest, OnConnectCallback) {
    auto transport = create_transport("tcp");
    Reactor reactor(std::move(transport));
    
    std::atomic<int> connect_count{0};
    reactor.on_connect([&](uint64_t conn_id) {
        connect_count++;
    });
    
    reactor.run_once(10);
    EXPECT_EQ(connect_count.load(), 0);
}

TEST(ReactorTest, OnDisconnectCallback) {
    auto transport = create_transport("tcp");
    Reactor reactor(std::move(transport));
    
    std::atomic<int> disconnect_count{0};
    reactor.on_disconnect([&](uint64_t conn_id) {
        disconnect_count++;
    });
    
    reactor.run_once(10);
    EXPECT_EQ(disconnect_count.load(), 0);
}

TEST(ReactorTest, RegisterHandler) {
    auto transport = create_transport("tcp");
    Reactor reactor(std::move(transport));
    
    std::atomic<int> heartbeat_count{0};
    reactor.register_handler<HeartbeatMessage>([&](uint64_t conn_id, const HeartbeatMessage& msg) {
        heartbeat_count++;
    });
    
    reactor.run_once(10);
    EXPECT_EQ(heartbeat_count.load(), 0);
}

TEST(ReactorTest, StopReactor) {
    auto transport = create_transport("tcp");
    Reactor reactor(std::move(transport));
    
    std::atomic<bool> stopped{false};
    
    std::thread t([&] {
        reactor.run();
        stopped = true;
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    reactor.stop();
    
    t.join();
    
    EXPECT_TRUE(stopped.load());
}

TEST(ReactorTest, SendMessage) {
    TCPTransport server;
    TCPTransport client;
    
    server.listen("127.0.0.1", 19010);
    
    Reactor server_reactor(std::make_unique<TCPTransport>());
    Reactor client_reactor(std::make_unique<TCPTransport>());
    
    uint64_t client_conn = client.connect("127.0.0.1", 19010);
    
    auto events = server.poll(500);
    EXPECT_GE(events.size(), 1);
    
    uint64_t server_conn = 0;
    for (const auto& ev : events) {
        if (ev.type == TransportEventType::CONNECT) {
            server_conn = ev.conn_id;
            break;
        }
    }
    EXPECT_GT(server_conn, 0);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    HeartbeatMessage msg;
    msg.header.type = MessageType::HEARTBEAT;
    msg.worker_id = 123;
    
    CMString encoded = MessageProtocol::encode(msg);
    client.send(client_conn, encoded);
    
    events = server.poll(500);
    bool found_data = false;
    for (const auto& ev : events) {
        if (ev.type == TransportEventType::DATA) {
            found_data = true;
            break;
        }
    }
    EXPECT_TRUE(found_data);
    
    server.close_all();
    client.close_all();
}

TEST(ReactorTest, SetIOThreadPool) {
    auto transport = create_transport("tcp");
    Reactor reactor(std::move(transport));
    
    auto pool = std::make_shared<IOThreadPool>(2);
    reactor.set_io_pool(pool);
    
    reactor.run_once(10);
}

}  // namespace fly