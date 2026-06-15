#include <gtest/gtest.h>
#include <network/cpp/transport_interface.h>
#include <network/cpp/epoll_multiplexer.h>
#include <network/cpp/tcp_socket.h>
#include <thread>
#include <chrono>

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

}  // namespace fly
