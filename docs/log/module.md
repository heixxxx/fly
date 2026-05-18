# Log 模块 — 日志系统

## 模块概述

**位置**: `src/log/`

提供分级日志记录，支持 Master 和 Worker 独立日志文件。

---

## 核心文件

| 文件 | 说明 |
|------|------|
| `cpp/logger.h/cpp` | Logger 类实现 |
| `export/log_export.cpp` | nanobind Python 导出 |

---

## 类说明

### Logger

```cpp
namespace fly {

enum class LogLevel : uint8_t {
    DEBUG = 0,
    INFO = 1,
    WARN = 2,
    ERROR = 3,
};

class Logger {
public:
    explicit Logger(const CMString& filename);
    ~Logger();

    void debug(const CMString& component, const CMString& msg);
    void info(const CMString& component, const CMString& msg);
    void warn(const CMString& component, const CMString& msg);
    void error(const CMString& component, const CMString& msg);

    void set_level(LogLevel level);
    LogLevel get_level() const;
    void flush();

    // 静态工厂方法
    static Logger* get_master();
    static Logger* get_worker(uint64_t worker_id);
    static void init_master(const CMString& path = "logs/");
    static void init_worker(uint64_t worker_id, const CMString& path = "logs/");
    static void shutdown();

private:
    CMString filename_;
    std::ofstream file_;
    std::mutex mutex_;
    LogLevel level_;

    void log(LogLevel level, const CMString& component, const CMString& msg);
    CMString level_str(LogLevel level) const;
    CMString timestamp() const;

    static CMMap<CMString, std::unique_ptr<Logger>> instances_;
    static std::mutex instance_mutex_;
    static CMString log_path_;
};

}
```

---

## 使用方式

### C++ 使用

```cpp
#include <log/cpp/logger.h>

fly::Logger::init_master("logs/");
fly::Logger* log = fly::Logger::get_master();
log->info("MasterAgent", "Worker registered: id=1");
log->debug("DataService", "Local index updated: key=xxx");
```

### Python 使用

```python
from _fly_log import init_master, init_worker

init_master("logs/")
init_worker(1, "logs/")
```

---

## 日志格式

```
[2026-05-17 10:30:45.123] [INFO ] [MasterAgent] Worker registered: id=1
[2026-05-17 10:30:46.456] [DEBUG] [DataService] Local index updated: key=output/result
[2026-05-17 10:30:47.789] [WARN ] [HeartbeatMonitor] Worker timeout: id=2
[2026-05-17 10:30:48.012] [ERROR] [WorkerAgent] Task execution failed: id=5
```

---

## 实现流程

```
init_master(path)
  → instances_["master"] = make_unique<Logger>(path + "master.log")

init_worker(worker_id, path)
  → instances_[f"worker_{id}"] = make_unique<Logger>(path + f"worker_{id}.log")

get_master() → instances_["master"]
get_worker(id) → instances_[f"worker_{id}"]

log(level, component, msg)
  → lock_guard(mutex_)
  → file_ << [timestamp] [level] [component] msg << endl
```

---

## 设计决策

| 决策 | 原因 |
|------|------|
| mutex 保护写入 | 多线程安全日志输出 |
| 静态工厂方法 | 按角色获取 Logger 实例，无需传递 |
| 分级日志 | 运行期可调整级别，减少不必要输出 |
| 独立文件 | Master/Worker 日志分离，便于问题定位 |
