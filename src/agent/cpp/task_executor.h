#pragma once

#include <common/cpp/common_types.h>
#include <cstdint>
#include <functional>

namespace fly {

enum class TaskExecStatus : uint8_t {
    SUCCESS = 0,
    FAILED = 1,
    TIMEOUT = 2,
};

struct TaskExecResult {
    uint64_t task_id;
    TaskExecStatus status;
    CMString output;
    CMString error;
    CMVector<CMString> outputs;
    CMVector<CMString> frozen_dbs;
};

class TaskExecutor {
public:
    using ExecFunc = std::function<TaskExecResult(
        uint64_t task_id, 
        const CMString& task_name,
        const CMString& task_module,
        const CMVector<CMString>& args)>;
    
    TaskExecutor();
    explicit TaskExecutor(ExecFunc exec_func);
    ~TaskExecutor();
    
    void set_exec_func(ExecFunc exec_func);
    void clear_exec_func();
    
    TaskExecResult execute(uint64_t task_id, const CMString& task_name,
                           const CMString& task_module, const CMVector<CMString>& args);
    bool is_running() const;
    
private:
    ExecFunc exec_func_;
    bool running_;
};

}  // namespace fly