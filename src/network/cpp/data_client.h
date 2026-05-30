#pragma once

#include <common/cpp/common_types.h>
#include <cstdint>
#include <tuple>

namespace fly {

// Blocking TCP client for worker-to-worker data transfer.
// Creates a separate connection per request — does NOT use the main Reactor.
// Thread-safe: each call creates its own socket.
class DataClient {
public:
    // Request compressed (raw disk) data from a remote worker.
    // Returns: (success, compressed_bytes, py_name, error_message)
    static std::tuple<bool, CMString, CMString, CMString> request_compressed_data(
        const CMString& host,
        int port,
        const CMString& object_name,
        int timeout_ms = 30000);
};

}  // namespace fly