# Log 模块 — 日志系统

## 模块概述

**位置**: `src/log/`

提供分级日志记录，采用单例模式，支持日志目录rotation和symlink指向最新日志。

---

## 核心文件

| 文件 | 说明 |
|------|------|
| `cpp/logger.h/cpp` | Logger 单例实现，rotation逻辑 |
| `export/log_export.cpp` | nanobind Python 导出 |

---

## 类说明

### Logger

```cpp
namespace fly {

enum class LogLevel : uint8_t {
    DEBUG = 0,
    INFO  = 1,
    WARN  = 2,
    ERROR = 3,
};

class Logger {
public:
    // 单例访问
    static Logger& instance();

    // 初始化（统一入口）
    static void init(const CMString& base_dir, uint64_t worker_id = 0);

    // 日志目录rotation
    static CMString resolve_log_dir(const CMString& base_dir);

    // 关闭
    static void shutdown();

    // 日志方法（单参数，无component）
    void debug(const CMString& msg);
    void info(const CMString& msg);
    void warn(const CMString& msg);
    void error(const CMString& msg);

    // 控制
    void set_level(LogLevel level);
    void flush();

private:
    Logger();                                  // 默认构造（fallback）
    explicit Logger(const CMString& filename); // 实际构造
    void log(LogLevel level, const CMString& msg);
    CMString level_str(LogLevel level) const;
    CMString timestamp() const;
    static void _update_latest_symlink(const CMString& target_dir, const CMString& base_dir);

    static Logger* instance_;                  // 单例指针
    CMString filename_;
    std::ofstream file_;
    std::mutex mutex_;
    LogLevel level_ = LogLevel::DEBUG;
};

}  // namespace fly

// 全局宏（推荐使用）
#define DBG(msg)   fly::Logger::instance().debug(msg)
#define INFO(msg)  fly::Logger::instance().info(msg)
#define WARN(msg)  fly::Logger::instance().warn(msg)
#define ERR(msg)   fly::Logger::instance().error(msg)
```

---

## Python API

```python
from _fly_log import (
    init_log,       # init_log(base_dir, worker_id=0)
    shutdown_log,   # shutdown_log()
    flush_log,      # flush_log()
    set_log_level,  # set_log_level(level: int)
    DBG,            # DBG(msg)
    INFO,           # INFO(msg)
    WARN,           # WARN(msg)
    ERR,            # ERR(msg)
)

# 初始化
init_log("fly_log", worker_id=0)  # Master
init_log("fly_log", worker_id=1)  # Worker 1

# 使用
INFO("Worker registered: id=1")
DBG("Local index updated: key=xxx")
WARN("Worker timeout: id=2")
ERR("Task execution failed: id=5")

# 关闭
shutdown_log()
```

---

## 日志格式

```
[2026-05-21 10:30:45.123] [INFO] Worker registered: id=1
[2026-05-21 10:30:46.456] [DEBUG] Local index updated: key=output/result
[2026-05-21 10:30:47.789] [WARN] Worker timeout: id=2
[2026-05-21 10:30:48.012] [ERROR] Task execution failed: id=5
```

---

## 日志目录Rotation

```
init("fly_log", worker_id=0)
  → resolve_log_dir("fly_log")
    → 若 fly_log 不存在: 创建 fly_log
    → 若 fly_log 存在:
        - 创建 fly_log.1, fly_log.2, ... (递增序号)
        - fly_log 内容移动到 fly_log.1
        - fly_log 重置为空目录
    → 返回实际目录名 (fly_log 或 fly_log.N)
  → _update_latest_symlink(target_dir, "fly_log")
    → 创建 fly_log.latest symlink → target_dir
  → instance_ = new Logger(target_dir + "/master.log")

init("fly_log", worker_id=1)
  → 日志文件: fly_log/worker1.log
```

**文件结构示例**:
```
fly_log/           # 当前日志目录
├── master.log
├── worker1.log
├── worker2.log
fly_log.1/         # 历史（第1次启动）
├── master.log
├── worker1.log
fly_log.2/         # 历史（第2次启动）
├── master.log
fly_log.latest -> fly_log/  # symlink指向当前
```

---

## 实现流程

```
init(base_dir, worker_id)
  → resolve_log_dir(base_dir)      # rotation逻辑
  → _update_latest_symlink(...)    # 更新symlink
  → delete instance_               # 清理旧实例
  → instance_ = new Logger(file)   # 创建新实例

instance()
  → if instance_ == nullptr: return static fallback
  → return *instance_

log(level, msg)
  → lock_guard(mutex_)
  → file_ << [timestamp] [level] msg << endl

shutdown()
  → delete instance_
  → instance_ = nullptr
```

---

## 设计决策

| 决策 | 原因 |
|------|------|
| 单例模式 | 进程级统一日志，简化使用 |
| 目录rotation | 保留历史日志，每次启动独立目录 |
| symlink | 快速定位最新日志 (`fly_log.latest`) |
| 全局宏 | C++ 代码简洁调用 (`DBG/INFO/WARN/ERR`) |
| 统一init | Master/Worker 共用 `init(base_dir, worker_id)` |
| 无component参数 | 简化API，日志内容自带上下文 |