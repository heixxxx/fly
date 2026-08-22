#pragma once

#include <common/cpp/common_types.h>
#include <monitor/cpp/monitor_types.h>
#include <cstdint>
#include <functional>

namespace fly {

enum class TaskExecStatus : uint8_t {
    SUCCESS = 0,
    FAILED = 1,
    TIMEOUT = 2,
};

struct TaskExecResult {
    uint64_t task_id_;
    TaskExecStatus status_;
    CMString output_;
    CMString error_;
    CMVector<CMString> outputs_;
    CMVector<CMString> frozen_dbs_;
    // ---- cluster monitor：Python io_stats 胶水解析填充（缺失/异常路径全 0）----
    uint64_t read_time_ms_ = 0;    // read_object 累计耗时
    uint64_t write_time_ms_ = 0;   // write_object + drain 落盘累计耗时
    uint64_t read_bytes_ = 0;      // 解压后读字节（pickle 路径精确）
    uint64_t io_mem_peak_rss_ = 0; // IO 时刻最大 RSS（read 结束/write 前采样；补入
                                   // TaskResourceTracker 窗口的峰值观测点）
    CMVector<ObjectIoRecord> io_items_;  // 对象级明细（MonitorTaskIoMessage 上报）
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