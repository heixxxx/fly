#pragma once

#include <common/cpp/common_types.h>
#include <common/cpp/error_types.h>
#include <functional>
#include <cstdint>

namespace fly {

void graceful_exit(const CMString& reason,
                   TaskErrorType error_type = TaskErrorType::UNKNOWN,
                   int exit_code = 1);

void register_shutdown_callback(std::function<void()> callback);

}  // namespace fly
