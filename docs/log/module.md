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
    Logger();

    // 单例访问（引用）
    static Logger& instance();

    // 单例访问（shared_ptr）
    static CMSharedPtr<Logger> instance_ptr();

    // 初始化（原地配置单例对象，不创建新对象）
    static void init(const CMString& base_dir, uint64_t worker_id = 0);

    // 日志目录rotation
    static CMString resolve_log_dir(const CMString& base_dir);

    // 关闭文件（对象不释放，仍可输出到 stderr）
    static void shutdown();

    // 日志方法
    void vlog(LogLevel level, fmt::string_view fmt, fmt::format_args args);
    void log(LogLevel level, const CMString& msg);

    // 控制
    void set_level(LogLevel level);
    void flush();

private:
    CMString level_str(LogLevel level) const;
    CMString timestamp() const;
    static void _update_latest_symlink(const CMString& target_dir, const CMString& base_dir);
    static CMString _ensure_trailing_sep(const CMString& path);

    CMString filename_;
    std::ofstream file_;
    std::mutex mutex_;
    LogLevel level_ = LogLevel::DEBUG;
    bool dual_output_ = false;
};

}  // namespace fly

// 全局宏（推荐使用）
#define DBG(fmt, ...)   fly::log_write(fly::LogLevel::DEBUG, FMT_STRING(fmt), ##__VA_ARGS__)
#define INFO(fmt, ...)  fly::log_write(fly::LogLevel::INFO,  FMT_STRING(fmt), ##__VA_ARGS__)
#define WARN(fmt, ...)  fly::log_write(fly::LogLevel::WARN,  FMT_STRING(fmt), ##__VA_ARGS__)
#define ERR(fmt, ...)   fly::log_write(fly::LogLevel::ERROR, FMT_STRING(fmt), ##__VA_ARGS__)
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
  → 配置 instance_ptr() 单例对象打开 target_dir + "/master.log"

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
instance_ptr()
  → static CMSharedPtr<Logger> inst = CMMakeShared<Logger>()  (首次调用构造一次，永不释放)

instance()
  → return *instance_ptr()

init(base_dir, worker_id)
  → resolve_log_dir(base_dir)      # rotation逻辑
  → _update_latest_symlink(...)    # 更新symlink
  → auto inst = instance_ptr()     # 持有 shared_ptr 副本确保对象存活
  → lock_guard(mutex_)             # 加锁
  → flush + close 旧 file_
  → 配置新 filename_ / file_ / dual_output_  (原地修改，不创建新对象)

log(level, msg)
  → lock_guard(mutex_)
  → file_ << [timestamp] [level] msg << "\n"

shutdown()
  → auto inst = instance_ptr()     # 持有副本
  → lock_guard(mutex_)
  → flush + close file_            # 对象不释放，后续日志输出到 stderr
```

---

## 设计决策

| 决策 | 原因 |
|------|------|
| shared_ptr 单例 | 统一模式，需 shared_ptr 时调 `instance_ptr()` 直接获取 |
| 原地配置 init | 不创建新对象，避免对象身份变化导致的引用/指针失效 |
| 对象永不释放 | static shared_ptr 析构在程序结束时自动发生，shutdown 只关闭文件，残留线程仍可输出到 stderr |
| 目录rotation | 保留历史日志，每次启动独立目录 |
| symlink | 快速定位最新日志 (`fly_log.latest`) |
| 全局宏 | C++ 代码简洁调用 (`DBG/INFO/WARN/ERR`) |
| fmt 格式化 | 支持自定义类型格式化 (`CM_FORMAT_CLASS` / `CM_FORMAT_ENUM`) |