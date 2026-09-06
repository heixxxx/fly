#pragma once

#include <container/cpp/container_aliases.h>
#include <cstdint>

namespace fly {

constexpr uint32_t EV_READ    = 0x1;
constexpr uint32_t EV_WRITE   = 0x2;
constexpr uint32_t EV_ONESHOT = 0x4;

struct IoEvent {
    int fd = -1;
    bool readable = false;
    bool writable = false;
    bool error = false;
    bool hangup = false;
};

class EpollMultiplexer {
public:
    virtual ~EpollMultiplexer() = default;

    virtual int create() = 0;
    virtual bool add(int epfd, int fd, uint32_t events) = 0;
    virtual bool mod(int epfd, int fd, uint32_t events) = 0;
    virtual bool del(int epfd, int fd) = 0;
    virtual int wait(int epfd, IoEvent* events, int max_events, int timeout_ms) = 0;
    virtual void destroy(int epfd) = 0;
};

CMSharedPtr<EpollMultiplexer> create_epoll_multiplexer();

}  // namespace fly
