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
| `src/core/py/__init__.py` | get_config() + Config 导出（合并了原 config.py） |

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

#### int 配置项

| 配置键 | 默认值 | 说明 |
|--------|--------|------|
| `worker_mode` | 0 | Worker 模式（0=普通, 1=standalone） |
| `worker_id` | 0 | Worker ID |
| `master_port` | 8000 | Master 监听端口 |
| `heartbeat_timeout` | 120 | 心跳超时（秒） |
| `heartbeat_interval` | 5 | 心跳间隔（秒） |
| `backup_threshold` | 100 | 备份阈值 |
| `auto_backup_enabled` | 0 | 自动备份开关（0=关闭, 1=开启） |
| `backup_replicas` | 2 | 备份副本数（包含原始文件） |
| `backup_decay_interval` | 300 | 降频检查间隔（秒），0=不降频 |
| `backup_decay_factor` | 50 | 降频因子（读取次数 *= factor/100） |
| `aggregation_threshold` | 1048576 | 写入聚合阈值（1MB） |
| `large_file_threshold` | 67108864 | 大文件阈值（64MB），已废弃，使用 `large_file_threshold_kb` |
| `large_file_threshold_kb` | 65536 | 大文件阈值（64MB，单位 KB，用户可配置） |
| `block_size` | 134217728 | 块大小（128MB） |
| `track_writes` | 0 | 是否启用写入跟踪（0=关闭, 1=开启） |
| `data_server_threads` | 1 | 数据传输线程池大小 |
| `compression_level` | 0 | 压缩级别 |
| `compression_threshold` | 4096 | 跳过压缩阈值（字节）。payload ≤ 此值时直接 passthrough 存储，避免小对象的压缩/解压开销。仅在阈值 < `serialize_chunk_size` 时生效 |
| `serialize_chunk_size` | 4194304 | 压缩流块大小（4MB） |
| `dependency_update_mode` | 0 | 依赖更新模式 |
| `interactive` | 0 | 交互模式（0=关闭, 1=开启） |
| `cli_master_port` | 0 | CLI 指定的 Master 端口 |
| `fail_unscheduleable_tasks` | 1 | 不可调度任务立即失败（1=立即fail并持久化, 0=保持等待） |
| `net_probe_enabled` | 1 | 网络感知远程读优先级总开关（1=开启, 0=关闭，TIER2 排序降级为 no-op） |
| `net_probe_interval_ms` | 30000 | 主动带宽探测周期（毫秒） |
| `net_probe_payload_kb` | 256 | 带宽探测 payload 大小（KB） |
| `net_probe_timeout_ms` | 3000 | 单次带宽探测超时（毫秒） |

#### string 配置项

| 配置键 | 默认值 | 说明 |
|--------|--------|------|
| `transport_type` | "tcp" | 传输层类型 |
| `compression_type` | "lz4" | 压缩算法（"lz4", "zstd", "none"） |
| `data_server_host` | "127.0.0.1" | 数据服务器监听地址 |
| `master_host` | "127.0.0.1" | Master 节点地址 |
| `log_dir` | "fly_log" | 日志目录 |
| `script_path` | "" | 脚本路径 |

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

    FLY_EXPORT_FUNCTION("ex_core_get_config", []() {
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
