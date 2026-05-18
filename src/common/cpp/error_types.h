#pragma once

#include <cstdint>

namespace fly {

enum class TaskErrorType : uint8_t {
    UNKNOWN = 0,
    EXECUTION_ERROR = 1,
    WRITE_TO_FROZEN_DB = 2,
    WRITE_REGISTRATION_FAILED = 3,
    WRITE_REGISTRATION_TIMEOUT = 4,
};

}  // namespace fly
