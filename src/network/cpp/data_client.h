#pragma once

#include <common/cpp/common_types.h>
#include <common/cpp/fly_buffer.h>
#include <cstdint>
#include <tuple>

namespace fly {

// Blocking TCP client for worker-to-worker data transfer.
// Creates a separate connection per request — does NOT use the main Reactor.
// Thread-safe: each call creates its own socket.
class DataClient {
public:
    // Request compressed (raw disk) data from a remote worker.
    // Returns: (success, FlyBufferPtr, py_name, write_context_hash, error_message)
    static std::tuple<bool, FlyBufferPtr, CMString, CMString, CMString> request_compressed_data(
        const CMString& host,
        int port,
        const CMString& object_name,
        uint64_t requesting_worker_id = 0,
        uint64_t request_id = 0,
        int timeout_ms = 300000);
};

}  // namespace fly