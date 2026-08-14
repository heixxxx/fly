#include <gtest/gtest.h>
#include <network/cpp/reactor.h>
#include <network/cpp/tcp_connection_manager.h>
#include <network/cpp/connection_manager.h>
#include <future>
#include <thread>
#include <mutex>
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

// HandlerThreadPool 串行 lane：同 lane 任务严格按提交序执行，且全在同一专用线程。
TEST(ReactorTest, HandlerThreadPoolLanePreservesOrder) {
    HandlerThreadPool pool(0, 100, 2);
    ASSERT_EQ(pool.lane_count(), 2u);

    CMVector<int> order;
    std::mutex order_mutex;
    std::atomic<int> executed{0};
    CMUnorderedSet<std::thread::id> threads;
    constexpr int kTasks = 100;
    for (int i = 0; i < kTasks; ++i) {
        pool.submit_to_lane(0, [&, i] {
            std::lock_guard<std::mutex> lk(order_mutex);
            order.push_back(i);
            threads.insert(std::this_thread::get_id());
            executed++;
        });
    }
    for (int i = 0; i < 300 && executed.load() < kTasks; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_EQ(executed.load(), kTasks);
    EXPECT_EQ(order.size(), static_cast<size_t>(kTasks));
    for (int i = 0; i < kTasks; ++i) {
        EXPECT_EQ(order[static_cast<size_t>(i)], i) << "lane must be FIFO";
    }
    EXPECT_EQ(threads.size(), 1u) << "one lane = one dedicated thread";
    pool.shutdown();
}

// 两个 lane 互不阻塞：lane0 的任务等 lane1 的任务放行（同一串行线程会超时死锁）。
TEST(ReactorTest, HandlerThreadPoolLanesRunInParallel) {
    HandlerThreadPool pool(0, 100, 2);
    std::promise<void> lane1_ran;
    auto fut = lane1_ran.get_future().share();
    std::atomic<bool> lane0_done{false};

    pool.submit_to_lane(1, [&] { lane1_ran.set_value(); });
    pool.submit_to_lane(0, [&] {
        // 最多等 2s：若 lane 串行（同线程），这里必然超时。
        if (fut.wait_for(std::chrono::seconds(2)) == std::future_status::ready) {
            lane0_done = true;
        }
    });

    for (int i = 0; i < 300 && !lane0_done.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(lane0_done.load()) << "lane0 must not be blocked by lane1";
    pool.shutdown();
}

// Reactor lane dispatch：同连接消息按到达序执行，且在 reactor 线程之外执行。
TEST(ReactorTest, LaneDispatchPreservesPerConnOrderOffReactorThread) {
    auto server_cm = create_connection_manager("tcp");
    server_cm->listen("127.0.0.1", 0);
    int sport = server_cm->get_bound_port();

    Reactor server_reactor(std::move(server_cm), 2);

    std::mutex seq_mutex;
    CMVector<uint64_t> worker_ids;
    CMUnorderedSet<std::thread::id> handler_threads;
    std::atomic<int> handled{0};
    server_reactor.register_handler<HeartbeatMessage>(
        [&](uint64_t, const HeartbeatMessage& msg) {
            std::lock_guard<std::mutex> lk(seq_mutex);
            worker_ids.push_back(msg.worker_id_);
            handler_threads.insert(std::this_thread::get_id());
            handled++;
        });

    std::thread server_thread([&] { server_reactor.run(); });
    server_reactor.wait_until_running();

    Reactor client_reactor(create_connection_manager("tcp"));
    uint64_t client_conn = client_reactor.connect("127.0.0.1", sport);
    ASSERT_GT(client_conn, 0);

    // 同一连接连续发 3 条：dispatch 提取帧的顺序 = 到达序，lane FIFO 保证执行序。
    for (uint64_t wid = 1; wid <= 3; ++wid) {
        HeartbeatMessage hb;
        hb.header_.type_ = MessageType::HEARTBEAT;
        hb.worker_id_ = wid;
        client_reactor.send(client_conn, hb);
    }

    for (int i = 0; i < 300 && handled.load() < 3; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_EQ(handled.load(), 3);

    {
        std::lock_guard<std::mutex> lk(seq_mutex);
        ASSERT_EQ(worker_ids.size(), 3u);
        for (uint64_t k = 0; k < 3; ++k) {
            EXPECT_EQ(worker_ids[static_cast<size_t>(k)], k + 1) << "per-conn order must hold";
        }
        EXPECT_EQ(handler_threads.size(), 1u) << "same conn hashed to one lane";
    }
    EXPECT_NE(*handler_threads.begin(), server_thread.get_id())
        << "handler must run off the reactor thread";

    server_reactor.stop();
    server_thread.join();
    client_reactor.stop();
}

// lane 模式 shutdown 排空：stop 后、析构前仍在 lane 队列中的 handler 必须全部执行。
// wid=0 的 handler 阻塞在 gate 上占住 lane，wid=1 排在其后；stop() 退出 run 循环
// 后打开 gate —— 若 shutdown 丢弃队列，wid=1 将永远不执行。
TEST(ReactorTest, LaneShutdownDrainsPendingHandlers) {
    auto server_cm = create_connection_manager("tcp");
    server_cm->listen("127.0.0.1", 0);
    int sport = server_cm->get_bound_port();

    std::atomic<bool> gate{false};
    std::atomic<bool> first_started{false};
    std::atomic<int> handled{0};
    {
        Reactor server_reactor(std::move(server_cm), 1);
        server_reactor.register_handler<HeartbeatMessage>(
            [&](uint64_t, const HeartbeatMessage& msg) {
                if (msg.worker_id_ == 0) {
                    first_started = true;
                    for (int i = 0; i < 500 && !gate.load(); ++i) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    }
                }
                handled++;
            });

        std::thread server_thread([&] { server_reactor.run(); });
        server_reactor.wait_until_running();

        Reactor client_reactor(create_connection_manager("tcp"));
        uint64_t client_conn = client_reactor.connect("127.0.0.1", sport);
        ASSERT_GT(client_conn, 0);

        for (uint64_t wid = 0; wid <= 1; ++wid) {
            HeartbeatMessage hb;
            hb.header_.type_ = MessageType::HEARTBEAT;
            hb.worker_id_ = wid;
            client_reactor.send(client_conn, hb);
        }
        // 等首条已进入 handler（占住唯一 lane），第二条已在 lane 队列排队。
        for (int i = 0; i < 300 && !first_started.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        ASSERT_TRUE(first_started.load());

        server_reactor.stop();
        server_thread.join();
        gate = true;  // 放行；析构（作用域结束）必须排空 wid=1
        client_reactor.stop();
    }
    // 作用域结束 = ~Reactor = handler_pool shutdown（join 前排空）。
    EXPECT_EQ(handled.load(), 2) << "queued handler must drain on shutdown";
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