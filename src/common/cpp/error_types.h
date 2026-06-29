#pragma once

#include <cstdint>

namespace fly {

enum class TaskErrorType {
    UNKNOWN = 0,
    EXECUTION_ERROR = 1,
    WRITE_TO_FROZEN_DB = 2,
    WRITE_REGISTRATION_FAILED = 3,
    WRITE_REGISTRATION_TIMEOUT = 4,
    WRITE_PROVENANCE_MISMATCH = 5,
    WRITE_DUPLICATE_SKIPPED = 6,
    DB_ALREADY_FROZEN = 7,   // freeze requested on an already-(pending-)frozen db (业务流程错误)
};

// Write operation result code. Independent from TaskErrorType (which is the
// task-execution-level cumulative state). WriteErrorType is the per-call return
// value of write_object / write_pickle_bytes — callers check it directly.
enum class WriteErrorType {
    OK = 0,                    // success
    FROZEN_DB = 1,             // database is frozen (read-only)
    REGISTRATION_FAILED = 2,   // write registration rejected (e.g. provenance mismatch)
    REGISTRATION_TIMEOUT = 3,  // write registration timed out
    DUPLICATE_SKIPPED = 4,     // duplicate write of same object (benign, not an error)
};

// Remote read result code. Returned by DataClientPool::request /
// DataClient::request_compressed_data and propagated through the
// DirectCompressedReadCallback to the TIER2 multi-replica retry loop in
// DataService::read_raw_compressed. The classification drives retry policy:
//
//   NONE            — success
//   DATA_NOT_READY  — transient: peer is still writing the object; the TIER2
//                     loop may retry indefinitely (data is expected to appear)
//   OBJECT_NOT_FOUND— permanent for THIS replica: this worker no longer holds
//                     the object; TIER2 removes the replica and moves on
//   NETWORK         — transient: connection / decode / protocol error; TIER2
//                     retries the replica but under a bounded deadline (a fully
//                     unreachable peer must not be retried forever)
//   SHUTDOWN        — permanent: the pool is being stopped; abort immediately,
//                     do not retry
enum class ReadError {
    NONE = 0,
    DATA_NOT_READY = 1,
    OBJECT_NOT_FOUND = 2,
    NETWORK = 3,
    SHUTDOWN = 4,
};

}  // namespace fly
