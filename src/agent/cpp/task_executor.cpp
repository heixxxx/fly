#include <agent/cpp/task_executor.h>

namespace fly {

TaskExecutor::TaskExecutor() : running_(false) {}

TaskExecutor::TaskExecutor(ExecFunc exec_func) : exec_func_(std::move(exec_func)), running_(false) {}

void TaskExecutor::set_exec_func(ExecFunc exec_func) {
    exec_func_ = std::move(exec_func);
}

TaskExecResult TaskExecutor::execute(uint64_t task_id, const CMString& task_name,
                                       const CMString& task_module, const CMVector<CMString>& args) {
    running_ = true;
    
    TaskExecResult result;
    result.task_id = task_id;
    
    if (exec_func_) {
        result = exec_func_(task_id, task_name, task_module, args);
    } else {
        result.status = TaskExecStatus::SUCCESS;
        result.output = task_module + "." + task_name;
    }
    
    running_ = false;
    return result;
}

bool TaskExecutor::is_running() const {
    return running_;
}

void TaskExecutor::cancel() {
    running_ = false;
}

}  // namespace fly