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
    transport.listen("127.0.0.1", 0);
    int port = transport.get_bound_port();
    EXPECT_GT(port, 0);
    EXPECT_NO_THROW(transport.stop_listening());
}

TEST(TCPTransportTest, ListenAndConnect) {
    TCPTransport server;
    TCPTransport client;

    server.listen("127.0.0.1", 0);
    int port = server.get_bound_port();

    uint64_t conn_id = client.connect("127.0.0.1", port);
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

    server.listen("127.0.0.1", 0);
    int port = server.get_bound_port();

    uint64_t client_conn = client.connect("127.0.0.1", port);

    auto server_events = server.poll(1000);
    ASSERT_GE(server_events.size(), 1);

    uint64_t server_conn = 0;
    for (const auto& ev : server_events) {
        if (ev.type == TransportEventType::CONNECT) {
            server_conn = ev.conn_id;
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
        if (ev.type == TransportEventType::DATA) {
            found_data = true;
            EXPECT_EQ(ev.data, msg);
            break;
        }
    }
    ASSERT_TRUE(found_data);

    server.close_all();
    client.close_all();
}

TEST(TCPTransportTest, ConnectionCount) {
    TCPTransport server;
    TCPTransport client;

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

TEST(TCPTransportTest, InvalidTransportType) {
    EXPECT_THROW(create_transport("udp"), std::runtime_error);
    EXPECT_THROW(create_transport("rdma"), std::runtime_error);
}

TEST(TCPTransportTest, MultipleConnections) {
    TCPTransport server;
    CMVector<std::unique_ptr<TCPTransport>> clients;

    server.listen("127.0.0.1", 0);
    int port = server.get_bound_port();

    for (int i = 0; i < 5; i++) {
        clients.push_back(CMMakeUnique<TCPTransport>());
        clients.back()->connect("127.0.0.1", port);
    }

    int connected = 0;
    for (int i = 0; i < 10 && connected < 5; i++) {
        auto events = server.poll(100);
        for (const auto& ev : events) {
            if (ev.type == TransportEventType::CONNECT) {
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

TEST(TCPTransportTest, LargeBufferSendRecv) {
    // Test sending a large buffer that may trigger partial send / EAGAIN path
    // This tests the poll+retry loop in TCPTransport::send() (fixed in bcf16aa)
    TCPTransport server;
    TCPTransport client;

    server.listen("127.0.0.1", 0);
    int port = server.get_bound_port();

    uint64_t client_conn = client.connect("127.0.0.1", port);

    auto server_events = server.poll(1000);
    ASSERT_GE(server_events.size(), 1);

    uint64_t server_conn = 0;
    for (const auto& ev : server_events) {
        if (ev.type == TransportEventType::CONNECT) {
            server_conn = ev.conn_id;
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
            if (ev.type == TransportEventType::DATA) {
                received += ev.data;
            }
        }
    }

    EXPECT_EQ(received.size(), large_msg.size());
    EXPECT_EQ(received, large_msg);

    server.close_all();
    client.close_all();
}

TEST(TCPTransportTest, MultipleLargeMessagesInSequence) {
    // Test multiple large messages sent in sequence
    TCPTransport server;
    TCPTransport client;

    server.listen("127.0.0.1", 0);
    int port = server.get_bound_port();

    uint64_t client_conn = client.connect("127.0.0.1", port);

    auto server_events = server.poll(1000);
    ASSERT_GE(server_events.size(), 1);

    uint64_t server_conn = 0;
    for (const auto& ev : server_events) {
        if (ev.type == TransportEventType::CONNECT) {
            server_conn = ev.conn_id;
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
            if (ev.type == TransportEventType::DATA) {
                received += ev.data;
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

TEST(TCPTransportTest, ConnectFdDoesNotContinuouslyFireEvents) {
    // Regression test for ff53939: connect() registered EPOLLOUT in level-triggered epoll,
    // continuously firing events. client_receiver consumed the event and closed the fd,
    // causing send() EBADF on the main thread. Fix: connect() now registers EPOLLIN only.
    TCPTransport server;
    TCPTransport client;

    server.listen("127.0.0.1", 0);
    int port = server.get_bound_port();

    client.connect("127.0.0.1", port);

    auto server_events = server.poll(500);
    ASSERT_GE(server_events.size(), 1);

    uint64_t server_conn = 0;
    for (const auto& ev : server_events) {
        if (ev.type == TransportEventType::CONNECT) {
            server_conn = ev.conn_id;
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
            if (ev.type == TransportEventType::DATA) {
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

TEST(TCPTransportTest, AcceptedFdDoesNotContinuouslyFireEvents) {
    // Verify accepted connections also don't continuously fire events (EPOLLIN only).
    TCPTransport server;
    TCPTransport client;

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
            if (ev.type == TransportEventType::DATA) {
                false_events++;
            }
        }
    }

    EXPECT_EQ(false_events, 0)
        << "Server received unexpected DATA events — accepted fd may have incorrect EPOLL flags";

    server.close_all();
    client.close_all();
}

}  // namespace fly