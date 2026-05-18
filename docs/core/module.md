# Core 模块 — 核心基础配置

## 模块概述

**位置**: `src/core/cpp/`

提供全局单例配置管理，控制框架运行时行为。

---

## 核心文件

| 文件 | 说明 |
|------|------|
| `config.h` | Config 类声明 |
| `config.cpp` | Config 类实现 |
| `src/core/export/core_export.cpp` | nanobind Python 导出 |
| `src/fly/config.py` | Python 封装 |

---

## 类说明

### Config（全局单例）

```cpp
class Config {
public:
    static Config& instance();          // 获取单例

    void set_int(const CMString& key, int64_t value);
    void set_str(const CMString& key, const CMString& value);

    int64_t get_int(const CMString& key) const;
    const CMString& get_str(const CMString& key) const;

    void mark_workers_launched();        // 标记 Worker 已启动，此后不可修改
    bool is_workers_launched() const;
    void reset();                        // 测试用重置

private:
    CMMap<CMString, int64_t> int_values_;
    CMMap<CMString, CMString> str_values_;
    bool workers_launched_ = false;
};
```

### 配置项一览

| 配置键 | 类型 | 默认值 | 说明 |
|--------|------|--------|------|
| `master_port` | int | 8000 | Master 监听端口 |
| `heartbeat_timeout` | int | 120 | 心跳超时（秒） |
| `heartbeat_interval` | int | 5 | 心跳间隔（秒） |
| `backup_threshold` | int | 100 | 备份阈值 |
| `aggregation_threshold` | int | 1048576 | 写入聚合阈值（1MB） |
| `large_file_threshold` | int | 10485760 | 大文件阈值（10MB） |
| `block_size` | int | 134217728 | 块大小（128MB） |
| `track_writes` | int | 0 | 是否启用写入跟踪 |
| `data_server_threads` | int | 1 | 数据传输线程池大小 |

---

## 实现流程

### 配置生命周期

```
1. 程序启动 → Config::instance() 静态初始化 → 加载默认值
2. 用户代码 → config.set_int("heartbeat_timeout", 120) → 更新配置
3. launch_workers → config.mark_workers_launched() → 锁定配置
4. 后续 set_* → 抛出 RuntimeError("Config must be set before workers are launched")
```

### Python 导出

```cpp
FLY_EXPORT_MODULE(_fly_core) {
    FLY_EXPORT_CLASS(Config, "EXCoreConfig")
        FLY_EXPORT_INIT()
        FLY_EXPORT_METHOD("set_int", &Config::set_int)
        FLY_EXPORT_METHOD("set_str", &Config::set_str)
        FLY_EXPORT_METHOD("get_int", &Config::get_int)
        FLY_EXPORT_METHOD("get_str", &Config::get_str);

    FLY_EXPORT_FUNCTION_REF("ex_core_get_config", []() -> Config& {
        return Config::instance();
    });
}
```

### Python 使用

```python
from fly import get_config

config = get_config()
config.set_int("heartbeat_timeout", 120)
config.set_int("track_writes", 1)
```

---

## 设计决策

| 决策 | 原因 |
|------|------|
| 静态局部变量单例 | 线程安全初始化，无需锁 |
| `mark_workers_launched` 保护 | 防止运行期修改导致不一致行为 |
| int/str 双 Map | 简单高效，避免变体类型开销 |
| 返回引用给 Python | Python 直接操作 C++ 单例，无拷贝 |
