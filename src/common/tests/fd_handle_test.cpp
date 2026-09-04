// FdHandle 契约测试（issue 011 M1，docs/issues/011-...md §4.2/§4.3）。
// 语义锚定：引用保活期间 fd 存活；shutdown 幂等且对端立即可见（写端
// EPIPE）；关闭恰好一次（并发掉落引用也不多不少）；closer 注入生效。
#include <gtest/gtest.h>
#include <common/cpp/fd_handle.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <cerrno>
#include <latch>
#include <vector>

namespace fly {
namespace {

// 建一对真实 socket（shutdown/写错误的语义载体）。
void make_socket_pair(int fds[2]) {
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
}

bool fd_alive(int fd) {
    return ::fcntl(fd, F_GETFD) != -1 || errno != EBADF;
}

TEST(FdHandleTest, ShutdownIdempotentAndFdStaysOpen) {
    int sv[2];
    make_socket_pair(sv);
    auto h = FdHandle::adopt(sv[0]);
    EXPECT_TRUE(h->shutdown());
    EXPECT_FALSE(h->shutdown());          // 幂等：第二次为 no-op 返 false
    EXPECT_TRUE(h->is_shutdown());
    EXPECT_GE(h->get(), 0);               // shutdown 不关闭（close 是引用计数层）
    EXPECT_TRUE(fd_alive(sv[0]));
    h.reset();
    EXPECT_FALSE(fd_alive(sv[0]));        // 析构后关闭
    ::close(sv[1]);
}

TEST(FdHandleTest, DestructorClosesEvenWithoutShutdown) {
    int sv[2];
    make_socket_pair(sv);
    auto h = FdHandle::adopt(sv[0]);
    h.reset();
    EXPECT_FALSE(fd_alive(sv[0]));
    ::close(sv[1]);
}

TEST(FdHandleTest, ReferenceKeepsFdOpen) {
    int sv[2];
    make_socket_pair(sv);
    auto h = FdHandle::adopt(sv[0]);
    auto kept = h;                        // 在途使用者持有引用
    h.reset();                            // 原持有人丢弃
    EXPECT_TRUE(fd_alive(sv[0])) << "引用保活期间 fd 不得关闭";
    kept.reset();                         // 最后引用释放才关闭
    EXPECT_FALSE(fd_alive(sv[0]));
    ::close(sv[1]);
}

TEST(FdHandleTest, WriteAfterShutdownGetsEpipe) {
    int sv[2];
    make_socket_pair(sv);
    auto h = FdHandle::adopt(sv[0]);
    EXPECT_TRUE(h->shutdown());
    // 对端在本端 shutdown 后写 → EPIPE（对端「连接已断」的错误方向正确）。
    // MSG_NOSIGNAL 与生产发送路径同防护（裸 write 会触发 SIGPIPE 杀进程）。
    EXPECT_EQ(::send(sv[1], "x", 1, MSG_NOSIGNAL), -1);
    EXPECT_EQ(errno, EPIPE);
    h.reset();
    ::close(sv[1]);
}

TEST(FdHandleTest, CloseNowThenDtorClosesExactlyOnce) {
    int sv[2];
    make_socket_pair(sv);
    int close_count = 0;
    auto h = FdHandle::adopt(sv[0], [&](int) { ++close_count; });
    h->close_now();
    EXPECT_EQ(h->get(), -1);              // 关闭后 get 按断连口径
    EXPECT_FALSE(h->shutdown());          // 已关闭：shutdown no-op
    h.reset();                            // 析构不得二次 close
    EXPECT_EQ(close_count, 1);
    ::close(sv[1]);
}

TEST(FdHandleTest, ConcurrentDropClosesExactlyOnce) {
    int sv[2];
    make_socket_pair(sv);
    int close_count = 0;
    std::mutex count_mutex;
    auto h = FdHandle::adopt(sv[0], [&](int fd) {
        std::lock_guard<std::mutex> lk(count_mutex);
        ++close_count;
    });
    constexpr int kThreads = 8;
    // 每线程持有自己的引用拷贝（真实用法形态）——对同一 shared_ptr 实例的
    // 并发 reset 本身是数据竞争（shared_ptr 实例非并发写安全，TSAN 实证），
    // 并发安全的是「各实例引用计数」的原子递减。
    CMVector<FdHandlePtr> slots(kThreads);
    for (int t = 0; t < kThreads; ++t) slots[t] = h;
    std::latch go{kThreads};
    CMVector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&slot = slots[t], &go] {
            go.count_down(); go.wait();
            slot.reset();                 // 并发掉落各自的最后引用
        });
    }
    h.reset();                            // 主线程引用同步掉落
    for (auto& t : threads) t.join();
    EXPECT_EQ(close_count, 1) << "并发析构必须恰好关闭一次";
    ::close(sv[1]);
}

TEST(FdHandleTest, CustomCloserRunsOnDestruct) {
    int sv[2];
    make_socket_pair(sv);
    int closed_fd = -1;
    auto h = FdHandle::adopt(sv[0], [&](int fd) { closed_fd = fd; });
    EXPECT_EQ(closed_fd, -1);
    h.reset();
    EXPECT_EQ(closed_fd, sv[0]) << "closer 收到原始 fd（池归还闭包依赖此契约）";
    ::close(sv[1]);
}

}  // namespace
}  // namespace fly
