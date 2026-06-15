#include <gtest/gtest.h>
#include <network/cpp/connection_manager.h>
#include <network/cpp/tcp_connection_manager.h>
#include <thread>
#include <chrono>

namespace fly {

TEST(ConnectionManagerTest, CreateTcpConnectionManager) {
    auto transport = create_connection_manager("tcp");
    EXPECT_NE(transport, nullptr);
    EXPECT_EQ(transport->connection_count(), 0);
}

TEST(TcpConnectionManagerTest, ListenAndStop) {
    TcpConnectionManager transport;
    transport.listen("127.0.0.1", 0);
    int port = transport.get_bound_port();
    EXPECT_GT(port, 0);
    EXPECT_NO_THROW(transport.stop_listening());
}

TEST(TcpConnectionManagerTest, ListenAndConnect) {
    TcpConnectionManager server;
    TcpConnectionManager client;

    server.listen("127.0.0.1", 0);
    int port = server.get_bound_port();

    uint64_t conn_id = client.connect("127.0.0.1", port);
    EXPECT_GT(conn_id, 0);

    auto server_events = server.poll(1000);
    EXPECT_GE(server_events.size(), 1);

    bool found_connect = false;
    for (const auto& ev : server_events) {
        if (ev.type_ == TransportEventType::CONNECT) {
            found_connect = true;
            EXPECT_GT(ev.conn_id_, 0);
            break;
        }
    }
    EXPECT_TRUE(found_connect);

    server.close_all();
    client.close_all();
}

TEST(TcpConnectionManagerTest, SendAndRecv) {
    TcpConnectionManager server;
    TcpConnectionManager client;

    server.listen("127.0.0.1", 0);
    int port = server.get_bound_port();

    uint64_t client_conn = client.connect("127.0.0.1", port);

    auto server_events = server.poll(1000);
    ASSERT_GE(server_events.size(), 1);

    uint64_t server_conn = 0;
    for (const auto& ev : server_events) {
        if (ev.type_ == TransportEventType::CONNECT) {
            server_conn = ev.conn_id_;
            break;
        }
    }
    ASSERT_GT(server_conn, 0);

    CMString msg = "hello from client";
    client.send(client_conn, msg);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    auto recv_events = server.poll(1000);
    bool found_data = false;
    for (const auto& ev : recv_events) {
        if (ev.type_ == TransportEventType::DATA) {
            found_data = true;
            EXPECT_EQ(ev.data_, msg);
            break;
        }
    }
    ASSERT_TRUE(found_data);

    server.close_all();
    client.close_all();
}

TEST(TcpConnectionManagerTest, ConnectionCount) {
    TcpConnectionManager server;
    TcpConnectionManager client;

    server.listen("127.0.0.1", 0);
    int port = server.get_bound_port();
    EXPECT_EQ(server.connection_count(), 0);

    client.connect("127.0.0.1", port);
    auto events = server.poll(1000);
    EXPECT_GE(events.size(), 1);

    EXPECT_EQ(server.connection_count(), 1);

    server.close_all();
    client.close_all();
}

TEST(TcpConnectionManagerTest, InvalidTransportType) {
    EXPECT_THROW(create_connection_manager("udp"), std::runtime_error);
    EXPECT_THROW(create_connection_manager("rdma"), std::runtime_error);
}

TEST(TcpConnectionManagerTest, MultipleConnections) {
    TcpConnectionManager server;
    CMVector<std::unique_ptr<TcpConnectionManager>> clients;

    server.listen("127.0.0.1", 0);
    int port = server.get_bound_port();

    for (int i = 0; i < 5; i++) {
        clients.push_back(CMMakeUnique<TcpConnectionManager>());
        clients.back()->connect("127.0.0.1", port);
    }

    int connected = 0;
    for (int i = 0; i < 10 && connected < 5; i++) {
        auto events = server.poll(100);
        for (const auto& ev : events) {
            if (ev.type_ == TransportEventType::CONNECT) {
                connected++;
            }
        }
    }

    EXPECT_EQ(connected, 5);

    server.close_all();
    for (auto& c : clients) {
        c->close_all();
    }
}

TEST(TcpConnectionManagerTest, LargeBufferSendRecv) {
    // Test sending a large buffer that may trigger partial send / EAGAIN path
    // This tests the poll+retry loop in TcpConnectionManager::send() (fixed in bcf16aa)
    TcpConnectionManager server;
    TcpConnectionManager client;

    server.listen("127.0.0.1", 0);
    int port = server.get_bound_port();

    uint64_t client_conn = client.connect("127.0.0.1", port);

    auto server_events = server.poll(1000);
    ASSERT_GE(server_events.size(), 1);

    uint64_t server_conn = 0;
    for (const auto& ev : server_events) {
        if (ev.type_ == TransportEventType::CONNECT) {
            server_conn = ev.conn_id_;
            break;
        }
    }
    ASSERT_GT(server_conn, 0);

    // Send 256KB buffer — likely to exceed socket send buffer, triggering partial sends
    CMString large_msg(256 * 1024, 'X');
    // Vary the data to avoid compression-like effects
    for (size_t i = 0; i < large_msg.size(); i++) {
        large_msg[i] = static_cast<char>(i % 256);
    }

    client.send(client_conn, large_msg);

    // Receive in multiple polls
    CMString received;
    for (int attempts = 0; attempts < 50 && received.size() < large_msg.size(); attempts++) {
        auto events = server.poll(200);
        for (const auto& ev : events) {
            if (ev.type_ == TransportEventType::DATA) {
                received += ev.data_;
            }
        }
    }

    EXPECT_EQ(received.size(), large_msg.size());
    EXPECT_EQ(received, large_msg);

    server.close_all();
    client.close_all();
}

TEST(TcpConnectionManagerTest, MultipleLargeMessagesInSequence) {
    // Test multiple large messages sent in sequence
    TcpConnectionManager server;
    TcpConnectionManager client;

    server.listen("127.0.0.1", 0);
    int port = server.get_bound_port();

    uint64_t client_conn = client.connect("127.0.0.1", port);

    auto server_events = server.poll(1000);
    ASSERT_GE(server_events.size(), 1);

    uint64_t server_conn = 0;
    for (const auto& ev : server_events) {
        if (ev.type_ == TransportEventType::CONNECT) {
            server_conn = ev.conn_id_;
            break;
        }
    }
    ASSERT_GT(server_conn, 0);

    // Send 3 messages of 64KB each
    CMVector<CMString> messages;
    for (int m = 0; m < 3; m++) {
        CMString msg(64 * 1024, static_cast<char>('A' + m));
        messages.push_back(msg);
        client.send(client_conn, msg);
    }

    // Wait for all data
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    CMString received;
    for (int attempts = 0; attempts < 20; attempts++) {
        auto events = server.poll(200);
        for (const auto& ev : events) {
            if (ev.type_ == TransportEventType::DATA) {
                received += ev.data_;
            }
        }
        size_t total = 3 * 64 * 1024;
        if (received.size() >= total) break;
    }

    size_t total_expected = 3 * 64 * 1024;
    EXPECT_EQ(received.size(), total_expected);

    // Verify each message is present (they're concatenated, each 64KB with unique char)
    for (int m = 0; m < 3; m++) {
        CMString expected(64 * 1024, static_cast<char>('A' + m));
        // Check that the expected pattern appears somewhere in received
        EXPECT_NE(received.find(expected), CMString::npos)
            << "Message " << m << " not found in received data";
    }

    server.close_all();
    client.close_all();
}

TEST(TcpConnectionManagerTest, ConnectFdDoesNotContinuouslyFireEvents) {
    // Regression test for ff53939: connect() registered EPOLLOUT in level-triggered epoll,
    // continuously firing events. client_receiver consumed the event and closed the fd,
    // causing send() EBADF on the main thread. Fix: connect() now registers EPOLLIN only.
    TcpConnectionManager server;
    TcpConnectionManager client;

    server.listen("127.0.0.1", 0);
    int port = server.get_bound_port();

    client.connect("127.0.0.1", port);

    auto server_events = server.poll(500);
    ASSERT_GE(server_events.size(), 1);

    uint64_t server_conn = 0;
    for (const auto& ev : server_events) {
        if (ev.type_ == TransportEventType::CONNECT) {
            server_conn = ev.conn_id_;
            break;
        }
    }
    ASSERT_GT(server_conn, 0);

    // After connection is established, poll with short timeout multiple times.
    // With the EPOLLOUT bug, client would continuously fire DATA events (false reads).
    // With EPOLLIN only (correct), no events fire until actual data arrives.
    int false_events = 0;
    for (int i = 0; i < 5; i++) {
        auto events = client.poll(10);  // 10ms timeout
        for (const auto& ev : events) {
            if (ev.type_ == TransportEventType::DATA) {
                false_events++;
            }
        }
    }

    // No false DATA events should fire on client when no data was sent
    EXPECT_EQ(false_events, 0)
        << "Client received unexpected DATA events — connect fd may be registered with EPOLLOUT";

    server.close_all();
    client.close_all();
}

TEST(TcpConnectionManagerTest, AcceptedFdDoesNotContinuouslyFireEvents) {
    // Verify accepted connections also don't continuously fire events (EPOLLIN only).
    TcpConnectionManager server;
    TcpConnectionManager client;

    server.listen("127.0.0.1", 0);
    int port = server.get_bound_port();

    client.connect("127.0.0.1", port);

    auto server_events = server.poll(500);
    ASSERT_GE(server_events.size(), 1);

    // After accepting, server should not fire spurious events
    int false_events = 0;
    for (int i = 0; i < 5; i++) {
        auto events = server.poll(10);  // 10ms timeout
        for (const auto& ev : events) {
            if (ev.type_ == TransportEventType::DATA) {
                false_events++;
            }
        }
    }

    EXPECT_EQ(false_events, 0)
        << "Server received unexpected DATA events — accepted fd may have incorrect EPOLL flags";

    server.close_all();
    client.close_all();
}

// connect() to an unlistened port must return 0 (failure sentinel), never throw.
// Covers the non-fatal-connect contract added in the connect-failure refactor.
TEST(TcpConnectionManagerTest, ConnectFailureReturnsZero) {
    TcpConnectionManager client;
    // Port 1 is reserved and unlistened on test machines; connect() fails synchronously.
    uint64_t conn_id = client.connect("127.0.0.1", 1);
    EXPECT_EQ(conn_id, 0u);
    EXPECT_EQ(client.connection_count(), 0u);
}

// close(conn_id) on a single connection + is_connected() state transitions.
TEST(TcpConnectionManagerTest, CloseSingleConnectionAndIsConnected) {
    TcpConnectionManager server;
    TcpConnectionManager client;

    server.listen("127.0.0.1", 0);
    int port = server.get_bound_port();

    uint64_t client_conn = client.connect("127.0.0.1", port);
    ASSERT_GT(client_conn, 0);
    EXPECT_TRUE(client.is_connected(client_conn));

    // Drain server accept so server-side conn is registered.
    auto events = server.poll(500);
    ASSERT_GE(events.size(), 1);
    uint64_t server_conn = 0;
    for (const auto& ev : events) {
        if (ev.type_ == TransportEventType::CONNECT) { server_conn = ev.conn_id_; break; }
    }
    ASSERT_GT(server_conn, 0);
    EXPECT_TRUE(server.is_connected(server_conn));

    // Close single connection on client.
    client.close(client_conn);
    EXPECT_FALSE(client.is_connected(client_conn));
    EXPECT_EQ(client.connection_count(), 0u);

    // Close unknown conn_id is a no-op (must not crash).
    EXPECT_NO_THROW(client.close(99999));

    // Cleanup server side.
    server.close(server_conn);
    EXPECT_FALSE(server.is_connected(server_conn));
}

// Peer closing the connection yields a DISCONNECT event on the other side.
// Covers poll()'s drain_socket-empty → DISCONNECT path (tcp_connection_manager.cpp L247-256).
TEST(TcpConnectionManagerTest, PeerCloseYieldsDisconnectEvent) {
    TcpConnectionManager server;
    TcpConnectionManager client;

    server.listen("127.0.0.1", 0);
    int port = server.get_bound_port();

    client.connect("127.0.0.1", port);

    // Accept on server.
    auto events = server.poll(500);
    ASSERT_GE(events.size(), 1);
    uint64_t server_conn = 0;
    for (const auto& ev : events) {
        if (ev.type_ == TransportEventType::CONNECT) { server_conn = ev.conn_id_; break; }
    }
    ASSERT_GT(server_conn, 0);

    // Client closes its end → server should observe DISCONNECT on next poll.
    client.close_all();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    bool found_disconnect = false;
    for (int attempt = 0; attempt < 10 && !found_disconnect; ++attempt) {
        auto evs = server.poll(200);
        for (const auto& ev : evs) {
            if (ev.type_ == TransportEventType::DISCONNECT && ev.conn_id_ == server_conn) {
                found_disconnect = true;
                break;
            }
        }
    }
    EXPECT_TRUE(found_disconnect)
        << "Server did not observe DISCONNECT after client closed";

    server.close_all();
}

}  // namespace fly