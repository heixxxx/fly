#include <agent/cpp/task_executor.h>
#include <Python.h>

namespace fly {

TaskExecutor::TaskExecutor() : running_(false) {}

TaskExecutor::TaskExecutor(ExecFunc exec_func) : exec_func_(std::move(exec_func)), running_(false) {}

TaskExecutor::~TaskExecutor() {
    clear_exec_func();
}

void TaskExecutor::set_exec_func(ExecFunc exec_func) {
    if (exec_func_) {
        if (Py_IsInitialized()) {
            PyGILState_STATE gstate = PyGILState_Ensure();
            exec_func_ = nullptr;
            PyGILState_Release(gstate);
        } else {
            exec_func_ = nullptr;
        }
    }
    exec_func_ = std::move(exec_func);
}

void TaskExecutor::clear_exec_func() {
    if (exec_func_) {
        if (Py_IsInitialized()) {
            PyGILState_STATE gstate = PyGILState_Ensure();
            exec_func_ = nullptr;
            PyGILState_Release(gstate);
        } else {
            exec_func_ = nullptr;
        }
    }
}

TaskExecResult TaskExecutor::execute(uint64_t task_id, const CMString& task_name,
                                       const CMString& task_module, const CMVector<CMString>& args) {
    running_ = true;
    
    TaskExecResult result;
    result.task_id_ = task_id;
    
    if (exec_func_) {
        result = exec_func_(task_id, task_name, task_module, args);
    } else {
        result.status_ = TaskExecStatus::SUCCESS;
        result.output_ = task_module + "." + task_name;
    }
    
    running_ = false;
    return result;
}

bool TaskExecutor::is_running() const {
    return running_;
}

}  // namespace fly