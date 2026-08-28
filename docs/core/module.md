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
    static CMSharedPtr<Config>& instance();  // 获取单例（shared_ptr 语义，支持 reset 重建）

    void set_int(const CMString& key, int64_t value);
    void set_str(const CMString& key, const CMString& value);

    int64_t get_int(const CMString& key) const;
    CMString get_str(const CMString& key) const;   // 按值返回（线程安全）

    void mark_workers_launched();        // 标记 Worker 已启动，此后不可修改
    bool is_workers_launched() const;
    void reset();                        // 测试用重置
    void save_to_file(const CMString& path) const;   // 持久化（master 同步给 worker）
    void load_from_file(const CMString& path);       // 启动时加载

private:
    mutable std::mutex mutex_;           // set/get 线程安全
    CMMap<CMString, int64_t> int_values_;
    CMMap<CMString, CMString> str_values_;
    bool workers_launched_ = false;
};
```

> 注意：`worker_mode` / `worker_id` / `master_port` / `master_host` / `data_server_host` /
> `script_path` / `interactive` / `cli_master_port` 是 **ProcessInfo**（每进程、不同步）的字段，
> 不是 Config 键——由 main.cpp 解析 CLI 后写入 ProcessInfo。详见 architecture.md「Config vs ProcessInfo」。

### 配置项一览

#### int 配置项

| 配置键 | 默认值 | 说明 |
|--------|--------|------|
| `heartbeat_timeout` | 120 | 心跳超时（秒） |
| `heartbeat_interval` | 5 | 心跳间隔（秒） |
| `auto_backup_enabled` | 0 | 自动备份开关（0=关闭, 1=开启） |
| `worker_suggest_count_threshold` | 100 | worker TIER2 累积读次数达此值触发 backup suggest |
| `worker_suggest_bytes_threshold` | 1073741824 | worker TIER2 累积传输字节（1GB）达此值触发 suggest |
| `worker_suggest_cooldown` | 60 | worker 两次 suggest 最小间隔（秒） |
| `master_ewma_decay_per_sec` | 1 | master EWMA 每秒衰减百分比（1 = 1%/s） |
| `backup_count_threshold` | 1000 | 每副本读次数达此值判定热点 |
| `backup_bytes_threshold` | 10737418240 | 每副本传输字节（10GB）达此值判定热点 |
| `max_backup_replicas` | 3 | 正常副本上限（含原始） |
| `backup_large_object_threshold` | 1073741824 | 大文件判定阈值（1GB，可触发例外突破上限） |
| `backup_high_score_threshold` | 107374182400 | 大文件 score_bytes（100GB）超此值触发例外 |
| `backup_extra_slots` | 2 | 例外情况下超出 max_backup_replicas 的额外副本数 |
| `aggregation_threshold` | 1048576 | 写入聚合阈值（1MB） |
| `large_file_threshold_kb` | 65536 | 大文件阈值（64MB，单位 KB，用户可配置） |
| `block_size` | 134217728 | 块大小（128MB） |
| `track_writes` | 0 | 是否启用写入跟踪（0=关闭, 1=开启） |
| `data_server_threads` | 4 | 数据传输线程池大小 |
| `compression_level` | 0 | 压缩级别 |
| `compression_threshold` | 4096 | 跳过压缩阈值（字节）。payload ≤ 此值时直接 passthrough 存储，避免小对象的压缩/解压开销。仅在阈值 < `serialize_chunk_size` 时生效 |
| `serialize_chunk_size` | 4194304 | 压缩流块大小（4MB） |
| `dependency_update_mode` | 0 | 依赖更新模式 |
| `fail_unscheduleable_tasks` | 1 | 不可调度任务立即失败（1=立即fail并持久化, 0=保持等待） |
| `net_probe_enabled` | 1 | 网络感知远程读优先级总开关（1=开启, 0=关闭，TIER2 排序降级为 no-op） |
| `net_probe_interval_ms` | 30000 | 主动带宽探测周期（毫秒） |
| `net_probe_payload_kb` | 256 | 带宽探测 payload 大小（KB） |
| `net_probe_timeout_ms` | 3000 | 单次带宽探测超时（毫秒） |
| `locality_scheduling_enabled` | 1 | 数据亲和调度开关（1=开启, 0=关闭） |
| `read_cache_size` | 1073741824 | ObjectCache 读缓存容量（1GB） |
| `temp_store_size` | 2147483648 | temp 淘汰磁盘溢出层容量（2GB） |
| `data_client_pool_size` | 4 | DataClientPool 并发上限 |
| `handler_lanes` | 4 | 消息 handler 并行 lane 数（同连接串行、跨连接并行）；0=全部内联（legacy 单线程 reactor） |
| `solver_openmp_threads` | 0 | solver C++ 核心 OpenMP 线程数（0=默认） |
| `worker_register_timeout` | 0 | **首次注册**占位符超时（秒）。默认 0=master 不等待不假设任何超时（worker 任意时刻注册都被接受）；>0 时超时未注册的占位符被 heartbeat 线程清理（WARN），并作为 `fly.wait_workers_registered()` 默认超时。worker 首连重试窗口同此键（两侧一致）。 |
| `worker_reconnect_timeout` | 120 | **断连重连**宽限窗口（秒），两侧对等：worker 断连后指数退避重连的总窗口（重连期间 task 在 worker 上继续执行、上报缓冲，重连后 flush）；master 对断连 worker 的判死宽限（宽限内 task 存活 RUNNING、不重调度、worker 状态保留、豁免心跳判死；超时判死 → task 重排队 + 数据全灭快速失败）。0=不重连不宽限（旧的"断连即死"逃生口）。master 挂=全群失败：worker 最多多活宽限窗口后干净退出。 |
| `worker_connect_retry_initial_ms` | 500 | connect 重试首次间隔（毫秒），指数 ×2 递增（单次上限 10s 硬编码）；首连与断连重连共用。 |
| `worker_register_ack_retry_initial_ms` | 500 | **注册守望**的超时退避初值（毫秒），指数 ×2 递增（单次上限 30s 硬编码）。P3-23 兜底：master 活着但 REGISTER/RegisterAck 被应用层吞掉时，watchdog 线程退避重发（幂等）；连接级丢失不走此路径（由 on_disconnect → reconnect_loop 事件驱动恢复）。 |

#### string 配置项

| 配置键 | 默认值 | 说明 |
|--------|--------|------|
| `transport_type` | "tcp" | 传输层类型 |
| `compression_type` | "lz4" | 压缩算法（"lz4", "zstd", "none"） |
| `log_dir` | "fly_log" | 日志目录 |
| `master_host` | "" | master advertise 地址（master 侧落盘 `.fly_config` 前写入；worker 引导的唯一寻址来源，CLI `--master-host` 仅调试覆盖） |
| `master_advertise_host` | "" | 多网卡集群显式指定计算网 IP（覆盖探测） |
| `master_port`（int） | 0 | master 定稿端口（master 侧落盘前写入；`--master-port` 仅调试覆盖） |

> `master_host` 双落点说明：**advertise 值**存 Config（随 `.fly_config` 下发给
> worker 引导），**master 自身运行视角**仍在 ProcessInfo（不同步）。
> `.fly_config` 首写完备（`Master.start()` 即落盘 + launch/expect 入口幂等
> 重写，原子写），local/ssh/bsub worker 统一 `--config-file` 引导。

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
| 静态局部变量单例 | 线程安全初始化（C++11 magic statics），与访问锁无关 |
| mutex_ 保护全部 get/set/reset/load | Config 是进程内共享单例：get_* 被 reactor/heartbeat/scheduler 多线程并发读，set_* 经 FFI 暴露给 Python 可运行时调用。无锁时并发 set 期间 unordered_map rehash 会导致读侧 UB（commit 6c82ec9） |
| get_str 返回 by value | 锁内拷贝后释放，避免调用方持引用期间并发 set_str 触发 rehash 造成悬空引用（现有调用方均已 by-value copy，零改动） |
| `mark_workers_launched` 保护 | 防止运行期修改导致不一致行为 |
| int/str 双 Map | 简单高效，避免变体类型开销 |
| all_ints()/all_strs() 不持锁 | 返回底层 map 引用仅供单线程调试/序列化快照，显式标注非线程安全 |
| 返回引用给 Python | Python 直接操作 C++ 单例，无拷贝（指 Python 绑定持有 Config 引用本身，get_str 值已拷贝） |
