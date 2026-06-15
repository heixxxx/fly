#include <network/cpp/epoll_multiplexer.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <cstring>

namespace fly {

namespace {
uint32_t to_epoll_events(uint32_t flags) {
    uint32_t ev = 0;
    if (flags & EV_READ)  ev |= EPOLLIN;
    if (flags & EV_WRITE) ev |= EPOLLOUT;
    if (flags & EV_ONESHOT) ev |= EPOLLONESHOT;
    return ev;
}

void from_epoll_event(const struct epoll_event& src, IoEvent& dst) {
    dst.fd = src.data.fd;
    dst.readable = src.events & EPOLLIN;
    dst.writable = src.events & EPOLLOUT;
    dst.error = src.events & EPOLLERR;
    dst.hangup = src.events & EPOLLHUP;
}
}

class EpollMultiplexerImpl : public EpollMultiplexer {
public:
    int create() override {
        return ::epoll_create1(EPOLL_CLOEXEC);
    }

    bool add(int epfd, int fd, uint32_t events) override {
        struct epoll_event ev;
        std::memset(&ev, 0, sizeof(ev));
        ev.events = to_epoll_events(events);
        ev.data.fd = fd;
        return ::epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) == 0;
    }

    bool mod(int epfd, int fd, uint32_t events) override {
        struct epoll_event ev;
        std::memset(&ev, 0, sizeof(ev));
        ev.events = to_epoll_events(events);
        ev.data.fd = fd;
        return ::epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev) == 0;
    }

    bool del(int epfd, int fd) override {
        return ::epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr) == 0;
    }

    int wait(int epfd, IoEvent* events, int max_events, int timeout_ms) override {
        struct epoll_event evs[64];
        int cap = max_events < 64 ? max_events : 64;
        int n = ::epoll_wait(epfd, evs, cap, timeout_ms);
        if (n < 0) return -1;
        for (int i = 0; i < n; ++i) {
            from_epoll_event(evs[i], events[i]);
        }
        return n;
    }

    void destroy(int epfd) override {
        if (epfd >= 0) {
            ::close(epfd);
        }
    }
};

CMSharedPtr<EpollMultiplexer> create_epoll_multiplexer() {
    return CMMakeShared<EpollMultiplexerImpl>();
}

}  // namespace fly
