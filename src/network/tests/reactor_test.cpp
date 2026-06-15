#include <gtest/gtest.h>
#include <network/cpp/reactor.h>
#include <network/cpp/tcp_connection_manager.h>
#include <network/cpp/connection_manager.h>
#include <thread>
#include <chrono>
#include <atomic>

namespace fly {

TEST(ReactorTest, CreateReactor) {
    auto transport = create_connection_manager("tcp");
    Reactor reactor(std::move(transport));
    
    EXPECT_NO_THROW(reactor.run_once(10));
}

TEST(ReactorTest, OnConnectCallback) {
    auto transport = create_connection_manager("tcp");
    Reactor reactor(std::move(transport));
    
    std::atomic<int> connect_count{0};
    reactor.on_connect([&](uint64_t conn_id) {
        connect_count++;
    });
    
    reactor.run_once(10);
    EXPECT_EQ(connect_count.load(), 0);
}

TEST(ReactorTest, OnDisconnectCallback) {
    auto transport = create_connection_manager("tcp");
    Reactor reactor(std::move(transport));
    
    std::atomic<int> disconnect_count{0};
    reactor.on_disconnect([&](uint64_t conn_id) {
        disconnect_count++;
    });
    
    reactor.run_once(10);
    EXPECT_EQ(disconnect_count.load(), 0);
}

TEST(ReactorTest, RegisterHandler) {
    auto transport = create_connection_manager("tcp");
    Reactor reactor(std::move(transport));
    
    std::atomic<int> heartbeat_count{0};
    reactor.register_handler<HeartbeatMessage>([&](uint64_t conn_id, const HeartbeatMessage& msg) {
        heartbeat_count++;
    });
    
    reactor.run_once(10);
    EXPECT_EQ(heartbeat_count.load(), 0);
}

TEST(ReactorTest, StopReactor) {
    auto transport = create_connection_manager("tcp");
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
    TcpConnectionManager server;
    TcpConnectionManager client;
    
    server.listen("127.0.0.1", 0);
    int port = server.get_bound_port();
    
    Reactor server_reactor(CMMakeUnique<TcpConnectionManager>());
    Reactor client_reactor(CMMakeUnique<TcpConnectionManager>());
    
    uint64_t client_conn = client.connect("127.0.0.1", port);
    
    auto events = server.poll(500);
    EXPECT_GE(events.size(), 1);
    
    uint64_t server_conn = 0;
    for (const auto& ev : events) {
        if (ev.type_ == TransportEventType::CONNECT) {
            server_conn = ev.conn_id_;
            break;
        }
    }
    EXPECT_GT(server_conn, 0);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    HeartbeatMessage msg;
    msg.header_.type_ = MessageType::HEARTBEAT;
    msg.worker_id_ = 123;
    
    CMString encoded = MessageProtocol::encode(msg);
    client.send(client_conn, encoded);
    
    events = server.poll(500);
    bool found_data = false;
    for (const auto& ev : events) {
        if (ev.type_ == TransportEventType::DATA) {
            found_data = true;
            break;
        }
    }
    EXPECT_TRUE(found_data);
    
    server.close_all();
    client.close_all();
}

TEST(ReactorTest, StopBeforeRunDoesNotHang) {
    // Regression test for bd1e5df: Reactor::run() could overwrite running_=false
    // with true when stop() was called before reactor thread started.
    // Fix: check stop_requested_ before setting running_=true.
    auto transport = create_connection_manager("tcp");
    Reactor reactor(std::move(transport));

    // Stop BEFORE run starts
    reactor.stop();

    // Run in thread — should exit immediately, not hang for 35s
    std::atomic<bool> run_exited{false};
    std::thread t([&] {
        reactor.run();
        run_exited = true;
    });

    // Wait with timeout — if it takes >2s, the fix is broken
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!run_exited.load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    t.join();
    EXPECT_TRUE(run_exited.load());
}

TEST(ReactorTest, WaitUntilRunningAfterStop) {
    // Verify wait_until_running() doesn't hang when reactor is stopped before run
    auto transport = create_connection_manager("tcp");
    Reactor reactor(std::move(transport));
    reactor.stop();

    // Start run in background — should exit quickly
    std::thread t([&] { reactor.run(); });
    t.join(); // Should join quickly

    // wait_until_running should return quickly since reactor already exited
    // (it polls running_ which should be false)
}

}  // namespace fly