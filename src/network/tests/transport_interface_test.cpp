#include <gtest/gtest.h>
#include <network/cpp/transport_interface.h>
#include <network/cpp/epoll_multiplexer.h>
#include <network/cpp/tcp_socket.h>
#include <thread>
#include <chrono>
#include <atomic>
#include <future>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>

namespace fly {

class TransportTest : public ::testing::Test {
protected:
    CMSharedPtr<Transport> transport_;
    CMSharedPtr<EpollMultiplexer> epoll_mp_;

    void SetUp() override {
        transport_ = create_tcp_transport();
        epoll_mp_ = create_epoll_multiplexer();
    }
};

TEST_F(TransportTest, CreateTransport) {
    EXPECT_NE(transport_, nullptr);
}

TEST_F(TransportTest, CreateEpollMultiplexer) {
    EXPECT_NE(epoll_mp_, nullptr);
}

TEST_F(TransportTest, ListenAndAccept) {
    int listen_fd = transport_->create_listen_socket("127.0.0.1", 0);
    ASSERT_GE(listen_fd, 0);

    int port = transport_->get_port(listen_fd);
    ASSERT_GT(port, 0);

    std::thread client_thread([&]() {
        int client_fd = transport_->create_connection("127.0.0.1", port);
        ASSERT_GE(client_fd, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        transport_->close(client_fd);
    });

    int epfd = epoll_mp_->create();
    ASSERT_GE(epfd, 0);
    epoll_mp_->add(epfd, listen_fd, EV_READ);

    IoEvent events[16];
    int n = epoll_mp_->wait(epfd, events, 16, 2000);
    EXPECT_GE(n, 1);
    EXPECT_EQ(events[0].fd, listen_fd);
    EXPECT_TRUE(events[0].readable);

    int server_fd = transport_->accept_connection(listen_fd);
    ASSERT_GE(server_fd, 0);

    client_thread.join();
    transport_->close(server_fd);
    epoll_mp_->del(epfd, listen_fd);
    transport_->close(listen_fd);
    epoll_mp_->destroy(epfd);
}

TEST_F(TransportTest, SendAndRecv) {
    int listen_fd = transport_->create_listen_socket("127.0.0.1", 0);
    ASSERT_GE(listen_fd, 0);
    int port = transport_->get_port(listen_fd);

    int epfd = epoll_mp_->create();
    ASSERT_GE(epfd, 0);
    epoll_mp_->add(epfd, listen_fd, EV_READ);

    std::thread client_thread([&]() {
        int client_fd = transport_->create_connection("127.0.0.1", port);
        ASSERT_GE(client_fd, 0);
        transport_->set_recv_timeout(client_fd, 5000);

        const char* msg = "hello";
        ssize_t sent = transport_->send(client_fd, msg, 5);
        EXPECT_EQ(sent, 5);

        char buf[64] = {};
        ssize_t recvd = transport_->recv(client_fd, buf, sizeof(buf));
        EXPECT_EQ(recvd, 5);
        EXPECT_STREQ(buf, "world");

        transport_->close(client_fd);
    });

    IoEvent events[16];
    int n = epoll_mp_->wait(epfd, events, 16, 2000);
    EXPECT_GE(n, 1);

    int server_fd = transport_->accept_connection(listen_fd);
    ASSERT_GE(server_fd, 0);
    transport_->set_recv_timeout(server_fd, 5000);

    char buf[64] = {};
    ssize_t recvd = transport_->recv(server_fd, buf, sizeof(buf));
    EXPECT_EQ(recvd, 5);
    EXPECT_STREQ(buf, "hello");

    const char* reply = "world";
    ssize_t sent = transport_->send(server_fd, reply, 5);
    EXPECT_EQ(sent, 5);

    client_thread.join();
    transport_->close(server_fd);
    epoll_mp_->del(epfd, listen_fd);
    transport_->close(listen_fd);
    epoll_mp_->destroy(epfd);
}

TEST_F(TransportTest, SendAll) {
    int listen_fd = transport_->create_listen_socket("127.0.0.1", 0);
    ASSERT_GE(listen_fd, 0);
    int port = transport_->get_port(listen_fd);

    int client_fd = transport_->create_connection("127.0.0.1", port);
    ASSERT_GE(client_fd, 0);

    int epfd = epoll_mp_->create();
    epoll_mp_->add(epfd, listen_fd, EV_READ);
    IoEvent events[16];
    int n = epoll_mp_->wait(epfd, events, 16, 2000);
    ASSERT_GE(n, 1);

    int server_fd = transport_->accept_connection(listen_fd);
    ASSERT_GE(server_fd, 0);
    transport_->set_recv_timeout(server_fd, 5000);

    CMString big_data(1024 * 100, 'X');
    EXPECT_TRUE(transport_->send_all(client_fd, big_data.data(), big_data.size()));

    CMString received;
    received.resize(big_data.size());
    size_t total = 0;
    while (total < big_data.size()) {
        ssize_t r = transport_->recv(server_fd, &received[total], big_data.size() - total);
        if (r <= 0) break;
        total += static_cast<size_t>(r);
    }
    EXPECT_EQ(total, big_data.size());

    transport_->close(client_fd);
    transport_->close(server_fd);
    epoll_mp_->del(epfd, listen_fd);
    transport_->close(listen_fd);
    epoll_mp_->destroy(epfd);
}

TEST_F(TransportTest, NonblockingRecv) {
    int listen_fd = transport_->create_listen_socket("127.0.0.1", 0);
    ASSERT_GE(listen_fd, 0);
    int port = transport_->get_port(listen_fd);

    int epfd = epoll_mp_->create();
    ASSERT_GE(epfd, 0);
    epoll_mp_->add(epfd, listen_fd, EV_READ);

    int client_fd = transport_->create_connection("127.0.0.1", port);
    ASSERT_GE(client_fd, 0);

    IoEvent events[16];
    int n = epoll_mp_->wait(epfd, events, 16, 2000);
    EXPECT_GE(n, 1);

    int server_fd = transport_->accept_connection(listen_fd);
    ASSERT_GE(server_fd, 0);

    transport_->set_nonblocking(server_fd);

    char buf[64] = {};
    ssize_t recvd = transport_->recv(server_fd, buf, sizeof(buf));
    EXPECT_EQ(recvd, -1);

    transport_->close(client_fd);
    transport_->close(server_fd);
    epoll_mp_->del(epfd, listen_fd);
    transport_->close(listen_fd);
    epoll_mp_->destroy(epfd);
}

TEST_F(TransportTest, EpollMultiplexerBasic) {
    int epfd = epoll_mp_->create();
    ASSERT_GE(epfd, 0);

    int listen_fd = transport_->create_listen_socket("127.0.0.1", 0);
    ASSERT_GE(listen_fd, 0);

    EXPECT_TRUE(epoll_mp_->add(epfd, listen_fd, EV_READ));

    int port = transport_->get_port(listen_fd);

    std::thread client_thread([&]() {
        int client_fd = transport_->create_connection("127.0.0.1", port);
        ASSERT_GE(client_fd, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        transport_->close(client_fd);
    });

    IoEvent events[16];
    int n = epoll_mp_->wait(epfd, events, 16, 2000);
    EXPECT_GE(n, 1);
    EXPECT_EQ(events[0].fd, listen_fd);
    EXPECT_TRUE(events[0].readable);

    client_thread.join();

    int accepted_fd = transport_->accept_connection(listen_fd);
    EXPECT_GE(accepted_fd, 0);

    transport_->close(accepted_fd);
    epoll_mp_->del(epfd, listen_fd);
    transport_->close(listen_fd);
    epoll_mp_->destroy(epfd);
}

TEST_F(TransportTest, EpollWithData) {
    int epfd = epoll_mp_->create();
    ASSERT_GE(epfd, 0);

    int listen_fd = transport_->create_listen_socket("127.0.0.1", 0);
    ASSERT_GE(listen_fd, 0);
    EXPECT_TRUE(epoll_mp_->add(epfd, listen_fd, EV_READ));

    int port = transport_->get_port(listen_fd);

    std::thread client_thread([&]() {
        int client_fd = transport_->create_connection("127.0.0.1", port);
        ASSERT_GE(client_fd, 0);
        const char* msg = "test_data";
        transport_->send(client_fd, msg, 9);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        transport_->close(client_fd);
    });

    IoEvent events[16];
    int n = epoll_mp_->wait(epfd, events, 16, 2000);
    EXPECT_GE(n, 1);

    int accepted_fd = transport_->accept_connection(listen_fd);
    ASSERT_GE(accepted_fd, 0);
    transport_->set_nonblocking(accepted_fd);
    EXPECT_TRUE(epoll_mp_->add(epfd, accepted_fd, EV_READ | EV_ONESHOT));

    n = epoll_mp_->wait(epfd, events, 16, 2000);
    EXPECT_GE(n, 1);

    char buf[64] = {};
    ssize_t recvd = transport_->recv(accepted_fd, buf, sizeof(buf));
    EXPECT_GT(recvd, 0);

    client_thread.join();
    epoll_mp_->del(epfd, accepted_fd);
    transport_->close(accepted_fd);
    epoll_mp_->del(epfd, listen_fd);
    transport_->close(listen_fd);
    epoll_mp_->destroy(epfd);
}

TEST_F(TransportTest, SetOptions) {
    int listen_fd = transport_->create_listen_socket("127.0.0.1", 0);
    ASSERT_GE(listen_fd, 0);
    int port = transport_->get_port(listen_fd);

    int epfd = epoll_mp_->create();
    ASSERT_GE(epfd, 0);
    epoll_mp_->add(epfd, listen_fd, EV_READ);

    int client_fd = transport_->create_connection("127.0.0.1", port);
    ASSERT_GE(client_fd, 0);

    IoEvent events[16];
    int n = epoll_mp_->wait(epfd, events, 16, 2000);
    EXPECT_GE(n, 1);

    int server_fd = transport_->accept_connection(listen_fd);
    ASSERT_GE(server_fd, 0);

    EXPECT_NO_THROW(transport_->set_nodelay(server_fd));
    EXPECT_NO_THROW(transport_->set_nonblocking(server_fd));
    EXPECT_NO_THROW(transport_->set_recv_timeout(server_fd, 5000));
    EXPECT_NO_THROW(transport_->set_send_timeout(server_fd, 5000));

    transport_->close(client_fd);
    transport_->close(server_fd);
    epoll_mp_->del(epfd, listen_fd);
    transport_->close(listen_fd);
    epoll_mp_->destroy(epfd);
}

TEST_F(TransportTest, CloseInvalidFd) {
    EXPECT_NO_THROW(transport_->close(-1));
}

TEST_F(TransportTest, AcceptNonBlocking) {
    int listen_fd = transport_->create_listen_socket("127.0.0.1", 0);
    ASSERT_GE(listen_fd, 0);

    int fd = transport_->accept_connection(listen_fd);
    EXPECT_EQ(fd, -1);

    transport_->close(listen_fd);
}


// ════════════════════════════════════════════════════════════════════
// tcp_socket send_all/sendv 的 EAGAIN→poll→续发路径（阻塞态流控）与
// 对端关闭返回 false。用 AF_UNIX socketpair + 极小对端 SO_RCVBUF 制造
// 确定的发送端阻塞（8MB 数据 vs KB 级缓冲）。
// ════════════════════════════════════════════════════════════════════

TEST_F(TransportTest, SendAllEagainPollsThenResumes) {
    int sv[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
    int rcv = 4096;
    ASSERT_EQ(::setsockopt(sv[1], SOL_SOCKET, SO_RCVBUF, &rcv, sizeof(rcv)), 0);
    transport_->set_nonblocking(sv[0]);

    const size_t total = 8u << 20;  // 8MB：必然耗尽 KB 级对端缓冲 → EAGAIN
    CMString payload(total, '\0');
    for (size_t i = 0; i < total; ++i) {
        payload[static_cast<size_t>(i)] = static_cast<char>((i * 31 + 7) & 0xFF);
    }

    // 读线程：读满 half 后停住等放行（制造发送端确定阻塞），放行后读完全部。
    std::atomic<bool> release_reader{false};
    CMVector<char> got;
    std::mutex got_mtx;
    std::thread reader([&] {
        char tmp[65536];
        size_t acc = 0;
        while (acc < total) {
            ssize_t n = ::recv(sv[1], tmp, sizeof(tmp), 0);
            if (n <= 0) break;
            {
                std::lock_guard<std::mutex> lk(got_mtx);
                got.insert(got.end(), tmp, tmp + n);
            }
            acc += static_cast<size_t>(n);
            if (acc >= total / 2 && !release_reader.load()) {
                // 到半程即等主线程放行——此时发送端几乎必然已 EAGAIN。
                while (!release_reader.load()) std::this_thread::yield();
            }
        }
    });

    auto fut = std::async(std::launch::async, [&] {
        return transport_->send_all(sv[0], payload.data(), total);
    });
    // 给 send_all 一段阻塞期（对端半停 → 必然 EAGAIN → poll 等待），随后放行
    // reader——poll 必须随对端消费被唤醒并续发完成。
    (void)fut.wait_for(std::chrono::milliseconds(300));
    release_reader.store(true);
    ASSERT_EQ(fut.wait_for(std::chrono::seconds(10)), std::future_status::ready)
        << "send_all 卡死（poll 未随对端消费推进）";
    EXPECT_TRUE(fut.get());

    reader.join();
    std::lock_guard<std::mutex> lk(got_mtx);
    ASSERT_EQ(got.size(), total);
    EXPECT_EQ(std::memcmp(got.data(), payload.data(), total), 0)
        << "EAGAIN 分段续发后字节流必须完整一致";
    ::close(sv[0]);
    ::close(sv[1]);
}

TEST_F(TransportTest, SendvEagainPollsThenResumes) {
    int sv[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
    int rcv = 4096;
    ASSERT_EQ(::setsockopt(sv[1], SOL_SOCKET, SO_RCVBUF, &rcv, sizeof(rcv)), 0);
    transport_->set_nonblocking(sv[0]);

    const size_t half = 4u << 20;
    CMString a(half, 'A');
    CMString b(half, 'B');
    struct iovec iov[2];
    iov[0].iov_base = a.data();
    iov[0].iov_len = half;
    iov[1].iov_base = b.data();
    iov[1].iov_len = half;

    std::atomic<bool> release_reader{false};
    CMVector<char> got;
    std::mutex got_mtx;
    std::thread reader([&] {
        char tmp[65536];
        size_t acc = 0;
        while (acc < 2 * half) {
            ssize_t n = ::recv(sv[1], tmp, sizeof(tmp), 0);
            if (n <= 0) break;
            {
                std::lock_guard<std::mutex> lk(got_mtx);
                got.insert(got.end(), tmp, tmp + n);
            }
            acc += static_cast<size_t>(n);
            if (acc >= half && !release_reader.load()) {
                while (!release_reader.load()) std::this_thread::yield();
            }
        }
    });

    auto fut = std::async(std::launch::async, [&] {
        return transport_->sendv(sv[0], iov, 2);
    });
    (void)fut.wait_for(std::chrono::milliseconds(300));
    release_reader.store(true);
    ASSERT_EQ(fut.wait_for(std::chrono::seconds(10)), std::future_status::ready)
        << "sendv 卡死";
    EXPECT_TRUE(fut.get());

    reader.join();
    std::lock_guard<std::mutex> lk(got_mtx);
    ASSERT_EQ(got.size(), 2 * half);
    EXPECT_EQ(std::memcmp(got.data(), a.data(), half), 0);
    EXPECT_EQ(std::memcmp(got.data() + half, b.data(), half), 0);
    ::close(sv[0]);
    ::close(sv[1]);
}

// 对端关闭后 send_all 必须返回 false（EPIPE，MSG_NOSIGNAL 下不崩溃）。
// 注：sendv 的对端关闭分支不在本测试触发——sendv 走 ::writev（无
// MSG_NOSIGNAL），SIGPIPE 会直接杀死进程（已作为生产缺陷上报，见任务报告）。
TEST_F(TransportTest, SendAllFailsAfterPeerClose) {
    int sv[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
    transport_->set_nonblocking(sv[0]);
    ::close(sv[1]);  // 对端立即关闭

    std::this_thread::sleep_for(std::chrono::milliseconds(20));  // 等关闭就绪
    CMString blob(1 << 20, 'X');  // 1MB：写满（已关）对端即 EPIPE
    EXPECT_FALSE(transport_->send_all(sv[0], blob.data(), blob.size()));
    ::close(sv[0]);
}

}  // namespace fly
