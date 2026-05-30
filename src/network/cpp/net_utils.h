#pragma once

#include <cstddef>

namespace fly {

// Receive exact number of bytes with deadline-based timeout
bool net_recv_exact(int fd, char* buf, size_t len, int timeout_ms);

// Send all bytes, handling partial sends
// The caller should set SO_SNDTIMEO on the socket before calling this function.
bool net_send_all(int fd, const char* data, size_t len, int timeout_ms = 5000);

}  // namespace fly
