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

}  // namespace fly
