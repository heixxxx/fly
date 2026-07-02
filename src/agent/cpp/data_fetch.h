#pragma once

// Shared data-fetch helpers used by both MasterAgent and WorkerAgent.
// Extracted to eliminate the verbatim duplication of request_data_from_worker
// between the two agent classes (they differ only in the requesting worker_id).

#include <network/cpp/data_client.h>
#include <storage/cpp/data_reader.h>
#include <log/cpp/logger.h>
#include <utility>

namespace fly {

// Fetch compressed data from a worker's DataServer via a one-shot DataClient
// connection and reassemble into a ReadResult. worker_id defaults to 0
// (master's id); workers pass their own.
// Returns {true, ReadResult} on success, {false, ReadResult{}} on failure.
inline std::pair<bool, ReadResult> fetch_from_worker(
    const CMString& host, int32_t port,
    const CMString& object_name,
    uint64_t worker_id = 0) {

    auto [success, compressed_data, py_name, hash, error] =
        DataClient::request_compressed_data(host, port, object_name, worker_id, 0);

    if (!success) {
        ERR("request_data_from_worker failed for {}: {}", object_name, error);
        return {false, ReadResult{}};
    }

    ReadResult result;
    result.data_buffer_.assign(compressed_data->data(), compressed_data->data() + compressed_data->size());
    result.py_name_ = std::move(py_name);
    return {true, std::move(result)};
}

}  // namespace fly
