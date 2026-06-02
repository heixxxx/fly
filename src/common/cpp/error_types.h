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

}  // namespace fly
