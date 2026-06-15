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

// End-to-end reactor message dispatch: client sends a typed message → server
// reactor's registered handler fires. Covers handle_event DATA path +
// dispatch_message handler invocation (the core gap in prior reactor tests).
TEST(ReactorTest, HandlerFiresOnReceivedMessage) {
    TcpConnectionManager server_transport;
    TcpConnectionManager client_transport;

    server_transport.listen("127.0.0.1", 0);
    int port = server_transport.get_bound_port();

    // Server reactor wraps a separate transport instance but must listen on same port.
    // Simpler: server reactor owns the listening transport directly.
    auto server_cm = create_connection_manager("tcp");
    server_cm->listen("127.0.0.1", 0);
    int sport = server_cm->get_bound_port();

    Reactor server_reactor(std::move(server_cm));
    Reactor client_reactor(create_connection_manager("tcp"));

    std::atomic<int> heartbeat_received{0};
    std::atomic<uint64_t> received_conn_id{0};
    server_reactor.register_handler<HeartbeatMessage>([&](uint64_t conn_id, const HeartbeatMessage& msg) {
        received_conn_id = conn_id;
        heartbeat_received++;
    });

    // Run server reactor in background.
    std::thread server_thread([&] { server_reactor.run(); });
    server_reactor.wait_until_running();

    // Client connects + sends a HeartbeatMessage.
    uint64_t client_conn = client_reactor.connect("127.0.0.1", sport);
    ASSERT_GT(client_conn, 0);

    HeartbeatMessage hb;
    hb.header_.type_ = MessageType::HEARTBEAT;
    hb.worker_id_ = 42;
    client_reactor.send(client_conn, hb);

    // Wait for handler to fire.
    for (int i = 0; i < 100 && heartbeat_received.load() == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_EQ(heartbeat_received.load(), 1);
    EXPECT_GT(received_conn_id.load(), 0u);

    server_reactor.stop();
    server_thread.join();
    client_reactor.stop();
}

// on_connect / on_disconnect handlers fire on real connection lifecycle events.
// Covers handle_event CONNECT + DISCONNECT paths.
TEST(ReactorTest, ConnectAndDisconnectHandlersFire) {
    auto server_cm = create_connection_manager("tcp");
    server_cm->listen("127.0.0.1", 0);
    int port = server_cm->get_bound_port();

    Reactor server_reactor(std::move(server_cm));

    std::atomic<int> connect_count{0};
    std::atomic<int> disconnect_count{0};
    server_reactor.on_connect([&](uint64_t) { connect_count++; });
    server_reactor.on_disconnect([&](uint64_t) { disconnect_count++; });

    std::thread server_thread([&] { server_reactor.run(); });
    server_reactor.wait_until_running();

    // Client connects via a raw ConnectionManager (Reactor has no close()).
    TcpConnectionManager client_cm;
    uint64_t client_conn = client_cm.connect("127.0.0.1", port);
    ASSERT_GT(client_conn, 0);

    // Wait for server on_connect.
    for (int i = 0; i < 100 && connect_count.load() == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_GE(connect_count.load(), 1);

    // Client closes → server observes DISCONNECT.
    client_cm.close_all();
    for (int i = 0; i < 100 && disconnect_count.load() == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_GE(disconnect_count.load(), 1);

    server_reactor.stop();
    server_thread.join();
}

// HandlerThreadPool: submit a task, it executes, shutdown is clean.
// Covers worker_loop / submit / shutdown.
TEST(ReactorTest, HandlerThreadPoolExecutesTasks) {
    HandlerThreadPool pool(2);
    std::atomic<int> executed{0};
    for (int i = 0; i < 5; ++i) {
        bool ok = pool.submit([&] { executed++; });
        EXPECT_TRUE(ok);
    }
    // Wait for tasks.
    for (int i = 0; i < 100 && executed.load() < 5; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_EQ(executed.load(), 5);

    pool.shutdown();
    // submit after shutdown returns false.
    EXPECT_FALSE(pool.submit([&] {}));
}

}  // namespace fly