# 文档变更记录

---
---

## 2026-06-28: write register 可见性延迟（非 stream 模式 task 级原子性 WP2）

### 非 stream 模式下 mark_data_ready 延迟到 task 完成

`do_write_register` 拆分为校验段（provenance + frozen 检查，两种模式都即时）和可见性登记段
（mark_data_ready + update_remote_idx + schedule_tasks）。非 stream 模式下可见性登记
延迟到 `on_task_complete` 的 written_objects_ 统一处理，保证 task 失败回滚后下游 task
不会被错误调度。`on_task_complete` 非 stream 分支补齐 `update_dependency_location_cache`。

- `master_agent.cpp::do_write_register`：可见性登记段按 `streaming_mode` 分流（master 自写 worker_id_==0 强制即时，无 TaskCompleteMessage 触发时机）
- `master_agent.cpp::on_task_complete`：非 stream 分支补 `update_dependency_location_cache`
- `master_agent.h`：`on_task_complete`/`on_task_failed` 移至 public（供测试直接调用）

---

## 2026-06-28: Freeze 延迟可见 + ack 通道 + 崩溃恢复（WP1）

### freeze 通知双路径冗余消除 + 非 stream 模式 task 级原子性

freeze 从"差集推断 + 延迟补发"重构为"task 内主动即时通知 + 按 task_id 提交/回滚"。
非 stream 模式（`dependency_update_mode != 0`）下，freeze 在 task 内声明为 pending，
task 成功才迁移到 confirmed + 广播；task 失败/崩溃按 task_id 回滚（防永久死锁）。

- `message_types.h`：`DatabaseFreezeNotification` 新增 `task_id_`；新增 `DatabaseFreezeAckMessage`（success + error_type）；`DATABASE_FREEZE_ACK=39`
- `error_types.h`：新增 `TaskErrorType::DB_ALREADY_FROZEN=7`（冲突 fail-fast）
- `master_agent.h/cpp`：新增 `pending_frozen_dbs_`（map<db_id,task_id>）；`is_db_frozen` 改查 confirmed ∪ pending；新增 `is_db_pending_frozen` / `commit_pending_frozen` / `rollback_pending_frozen`；`on_database_freeze_request` 分流（stream 即时 / 非 stream pending）+ 冲突检测回 ack；`on_task_complete` 调 commit；`on_task_failed`/`on_disconnect` 调 rollback
- `worker_agent.h/cpp`：`request_database_freeze` 从 fire-and-forget 改同步等 ack（pending+cv，5s 超时）；`DatabaseFreezeNotification` 带当前 task_id；新增 `on_database_freeze_ack` handler + reactor 注册
- `executor.py`：删除 frozen 差集计算（遍历 `_db_cache` 两次 + 前后快照）；freeze 由即时通知 + task_id 提交负责

---

## 2026-06-28: 数据 Locality 调度 + 写入注册统一 + size 链路

### 数据 Locality 调度（Config `locality_scheduling_enabled`，默认 1 开启）

scheduler 按数据亲和度选 worker：对每个 ready task，计算各 worker 持有其输入数据的总量（score），
选 score 最大且不降低 capability 匹配质量的 idle worker。三阶段算法：capability 完整匹配优先 →
locality 偏好 → 兜底。scheduler 直接查 DataService placement 算分，持久 score 缓冲区复用。

- `task_scheduler.h/cpp`：`locality_enabled_` 开关、`compute_scores`（依赖驱动，score_buf_ 按 worker_id 索引）、`select_best_worker` 三阶段算法
- `dependency_graph.h`：`get_task_requirements` 改返回 `const TaskRequirements&`（无值拷贝）
- `core/config.cpp`：新增 `locality_scheduling_enabled`（默认 1）

### 写入注册统一到 WriteRegisterMessage（删除 DataReadyMessage）

所有写入注册（worker 写 / master 自写 / backup）统一走 `WriteRegisterMessage` → `do_write_register`。
删除冗余的 `DataReadyMessage`（其核心动作 mark_data_ready/update_remote_idx/schedule 已由 WriteRegister 覆盖）。
master 自写改为同步调 `do_write_register`（丢弃 ack，零网络开销）。

- `message_types.h`：删除 `DataReadyMessage` + `MessageType::DATA_READY`；`WriteRegisterMessage` 加 `writer_id_` + `size_bytes_`；新增 `WrittenObject` 结构体
- `master_agent.cpp`：抽 `do_write_register` 纯逻辑函数；`on_data_ready`/`on_master_record_write` 删除；`record_worker_info`/`evaluate_and_trigger_backup` 从原 on_data_ready 抽出迁入 do_write_register
- `worker_agent.cpp`：`record_write` 删除 streaming 分支（保留 `current_writes_` 收集）

### size 链路（RemoteObjectMeta.size_bytes_）

数据对象的压缩后字节数随写入注册传递到 master placement table，供 locality 调度亲和度打分。

- `data_service.h/cpp`：`RemoteObjectMeta` 加 `size_bytes_`；`update_remote_idx` 加 size 参数（size>0 才更新，size==0 保持原值，防御 rebuild 路径）；新增 `get_remote_size`
- `worker_context.h`：`register_write`/`record_write`/`set_register_func`/`set_record_write_func` 签名加 `int64_t compressed_size`
- `database.cpp`：`commit_write`/`do_backup_write`/`put_temp_data` 三处 register 调用带 size
- `TaskCompleteMessage.written_objects_` 改为 `CMVector<WrittenObject>`（含 size）

### 模块依赖

task 模块（本质是调度模块）新增对 storage 的依赖（scheduler 查 DataService placement 算分）。
`task/cpp/BUILD` 加 `fly_storage` 依赖，`fly_task_so` 用 `dynamic_deps` 引用 `fly_storage_so`。

---

## 2026-06-25: 失败 Task 脏数据清理（事务化段标记 + 异常清理）

### idx op log 事务化段标记

LocalIndex 新增 BEGIN/END/ABORT 三个段边界标记（不含 task_id）。worker task
写入被 BEGIN/END 包裹，ADD 进 pending 区，END 提交 / ABORT 回滚。崩溃遗留的
未闭合段在 load_db 时自动丢弃（pending 区语义）。

- `local_index.h/cpp`：新增 IdxOpType::BEGIN/END/ABORT、mark_begin/end/abort、
  had_unclosed_segment 诊断、load pending 区状态机
- `data_writer.h/cpp`：mark_begin 记录 data 偏移回滚点；abort_segment 执行
  data 文件 truncate（含跨 rollover 多文件）
- `write_back_queue.h/cpp`：新增 clear_pending 丢弃未落盘脏写（比 drain 高效）

### 异常清理路径

- `database.h/cpp`：abort_task_writes（clear_pending + ABORT + truncate + 清内存）
- `worker_agent.cpp`：BEGIN 在 task 首次写入打（WBQ execute lambda）；成功打 END；
  失败走 cleanup_failed_task_writes
- `worker_context.h`：新增 transaction_mode 区分 worker task 与 master 写入
- `message_types.h`：TaskFailedMessage 新增 dirty_objects_ 字段
- `master_agent.cpp`：on_task_failed 清理 dirty_objects 的 remote_idx/provenance/
  依赖图 + 广播 OBJECT_REMOVED

### 连带修复

- `master_agent.cpp`：on_task_failed 增加持久化 failed task（之前只有调度失败
  才持久化）；schedule_tasks 依赖不可解检测移到 fail_unscheduleable_tasks
  开关之前（修复上游失败后下游 pending task 40s 才判失败的延迟）

### 文档更新

- `docs/issues/001-failed-task-rerun-write-duplication.md`：状态改为 Resolved，
  追加最终解决方案章节
- `docs/storage/module.md`：补充写入事务语义 + WriteBackQueue clear_pending

### 测试补充（大对象 + 多对象跨文件）

- DataWriter 单测 +3：AbortLargeObjectInEmptyFile / AbortLargeObjectTriggersRollover /
  AbortMultipleObjectsAcrossFiles（验证 abort 的 data 文件 truncate 含跨 rollover 多文件）
- QA test_mixed_write_fail：大对象(>1MB)触发 rollover + 多小对象跨文件 →
  task 失败 abort → load_db 脏数据不恢复 → restart 大对象数据正确

---
---

## 2026-06-23: FlyStream C++ 基础设施 + __getstate__/__setstate__ + 宏重命名

### FlyStream — 流式序列化+压缩容器（C++ 基础设施）

新增 `FlyStream` 类（`src/storage/cpp/fly_stream.h`），流式序列化+压缩容器。
已导出到 Python（`_fly_storage.FlyStream`），但 database.py 暂未集成
（worker 模式 DataService remove 死锁问题待排查）。

### FLY_EXPORT_SERIALIZE 宏更新

补回标准 pickle 协议 `__getstate__` / `__setstate__`，支持 C++ 对象放入
Python 容器（list/dict/nested）。与 `__getstate_buffer__` /
`__setstate_from_buffer__`（零拷贝路径）共存。

### 序列化宏重命名

- `FLY_ENCODE_TO_BYTES` → `FLY_ENCODE_TO_BUFFER`（原名误导）
- `FLY_DECODE_FROM_BYTES` → `FLY_DECODE_FROM_BUFFER`

### stress_stability 测试修复

- 根因：`kMaxCompletedTasks=100` 限制 completed bucket，测试 150 task 超限
- 修复：减小测试规模到 90 task（60 writes + 30 sums）


## 2026-06-22: var 小数据存储服务 + get_obj_name→get_full_name 重命名

### var 小数据存储服务（db.set_var / get_var / remove_var）

新增轻量级小对象 KV 服务，绕开 `write_object` 的压缩/缓存/WriteBackQueue/依赖图全套机制：

- **db 由 db 直接管理**：Database 内建 `var_store_`（FlyBufferPtr 载体），master 进程 Database 实例为权威存储，worker 经 WorkerAgentContext 同步到 master。
- **零拷贝**：内存层全程 FlyBufferPtr 共享；消息边界用 `mutable` 字段 + `std::move`；Python 对象用 FlyBuffer 的 file-protocol（`pickle.dump(value, buf)` / `pickle.load(buf)` via readinto）；C++ 对象用 `__getstate_buffer__` / `__setstate_from_buffer__`。
- **全程全名**：var 名用 `db.get_full_name(name)`（`db_id:short_name`），消息无冗余 db_id，master 用 `split_full_name`（基于 db_id_len 固定切分）定位 Database。
- **隐式依赖**：set_var/get_var 同步，依赖 master reactor 单线程 FIFO 保证"set_var 后 write_object 的数据依赖满足时，var 一定可取"。
- **@as_task(vars=...)**：task 声明所需 var，master 调度时 inline 带入 TaskAssignMessage。
- **freeze 持久化**：freeze 时 `_VARS` 文件持久化未删除 var，load_db 恢复。
- **不变性**：var 写入后不可改，重复 set 被拒绝；freeze 后 set_var 被拒绝。

### get_obj_name → get_full_name 全仓重命名

var 与 object 共用 `db_id:short_name` 命名空间，`get_obj_name` 名称对 var 有歧义，统一为中性 `get_full_name`。涉及 solver/mapreduce/e2e_tasks/单测等 30+ 处。

### FlyBuffer file-protocol 接口

FlyBuffer 新增 `read(n)` / `readline()` / `readinto(bytearray)` / `seek(n)` / `pos`，支持作为 `pickle.load` 的 file-like 对象（readinto 零拷贝写入 pickle 工作缓冲），消除 var get_var 的中间 Python bytes 拷贝。

### executor 三阶段执行

重构 worker task 执行为 `preprocess`（db 创建/注册 + var 注入）/ `execute`（调用 task 函数）/ `postprocess`（空函数预留扩展点）。

### FLY_EXPORT_SERIALIZE 序列化接口

移除 `__getstate__`/`__setstate__`（bytes 版，无生产使用），改为 `__setstate_from_buffer__(FlyBufferPtr)`（零拷贝反序列化填充）。

---

## 2026-06-21: attribute_timeout — 属性依赖超时降级

### `@as_task(requires=...)` 支持 tuple/callable 形式

`requires` 参数扩展，新增属性依赖超时语义：

- `list[str]`：死等（旧行为，向后兼容）。
- `tuple(list[str], float)`：`(能力标签, 超时秒数)`，`timeout<0` 死等 / `==0` 立即降级 / `>0` 限时降级。
- `callable(*args, **kwargs)`：提交时动态解析为上述任一形式。

### 调度器限时降级机制

新增 master `attr_timeout_check_thread_`（周期 200ms）周期性触发 `schedule_tasks()`，让限时等待属性的 task 在数据依赖满足后到期被降级调度到匹配属性最多的 idle worker。

### 文档同步

- `docs/python-api/module.md`：更新 `as_task` 签名、requires 形式、attribute_timeout 语义。
- `docs/task/module.md`：调度算法段落补充属性匹配与超时降级表。

---

## 2026-06-20: solver 优化 + stop() 修复 + FLY_RELEASE + 粗网格预构建

### stop() 流程重构

**三阶段流程**：
1. Phase 1: 等待所有 running tasks 完成（workers 仍然活跃）
2. Phase 2: 发送 shutdown 给所有 workers
3. Phase 3: 等待 workers 断开连接（CV 通知机制）

**draining 模式修复**: on_disconnect 在 draining 模式下将 running tasks 标记为 FAILED（而非跳过），并通知 drain_cv_，避免 stop() 等待 10s 超时。

**自动 stop()**: 脚本模式下，用户脚本执行完毕后自动调用 stop()。交互模式下，用户退出时通过 atexit 调用 stop()。

### FLY_RELEASE 编译 flag

新增 `FLY_RELEASE` 编译宏，在 `build:opt` 模式下自动定义。DBG 宏在 FLY_RELEASE 模式下编译为空宏 `((void)0)`，彻底消除热路径日志开销。

配置方式：`./fly.sh build --config=opt`（等效于 `--compilation_mode=opt -DFLY_RELEASE`）

### scipy 模块级 import

将 `numpy`、`scipy.sparse`、`scipy.sparse.linalg.splu` 移到 `ras_graph.py` 顶部，避免热路径懒加载。Worker 进程启动时即完成 import，不阻塞迭代。

### 粗网格预构建

coarse 校正的粗网格构建从迭代循环内移到迭代前。通过 `_prebuild_coarse_grid()` 向所有 worker 分发构建任务，worker 并行构建，不阻塞 check task。

### 热路径日志降级

- `data_server.cpp`: DS-ACCEPT/DS-Q/DS-SEND INFO→DBG
- `master_agent.cpp`: WriteRegister INFO→DBG

### 性能对比 (O2 + FLY_RELEASE, golden_n50_sd9)

| 版本 | Wall Clock | t_total | read_nb | write |
|------|-----------|---------|---------|-------|
| Baseline | 5578ms | 10.7ms | 6.2ms | 2.0ms |
| 优化后 | 3368ms | 4.8ms | 2.4ms | 1.0ms |
| 提升 | -39.6% | -55% | -61% | -50% |

### n=500 coarse 性能

| 阶段 | 优化前 | 优化后 |
|------|--------|--------|
| 粗网格构建 | 迭代内阻塞 | 1.6s (迭代前并行) |
| 迭代时间 | 5.7s | 2.8s (-51%) |
| 总时间 | 7.44s | 6.41s (-14%) |

---

## 2026-06-19: TaskManager/DependencyGraph 性能优化 + 依赖位置预取

### TaskManager 优化

**按状态分桶存储**: 任务元数据按状态分为 5 个桶（PENDING/RUNNING/COMPLETED/FAILED/CANCELLED），按状态查询从 O(n) 降到 O(k)。

**shared_ptr 存储**: 任务元数据使用 shared_ptr 管理，读取时返回 shared_ptr 拷贝（0.2ns），消除数据竞态。

**原子复合操作**: 将常见的多步操作（如更新状态+设置错误）合并为单次锁获取，减少锁竞争。

**O(1) 状态查询**: 新增 has_tasks_with_status、count_tasks_by_status 等 O(1) 查询接口。

**ID-only 查询**: 新增 get_task_ids_by_status、get_task_ids_by_worker，避免拷贝完整元数据。

**自动清理**: 已完成任务超过阈值时自动淘汰最老任务，防止内存无限增长。

### DependencyGraph 反向索引

新增 data → pending_tasks 的反向索引。mark_data_ready 从遍历所有 pending tasks（O(P×D)）改为只检查依赖该数据的任务（O(T×D)）。

### 依赖位置预取

**机制**: Master 在任务提交时缓存依赖数据位置，在任务分配时通过 TaskAssignMessage 下发给 Worker。Worker 读取依赖数据时优先使用预取位置，避免查询 Master。

**效果**: 远程读路径从 2 次网络往返（Master 查询 + 数据读取）减少为 1 次（仅数据读取）。

### sendv 合并发送

DataServer 使用 writev 系统调用将 header 和 payload 合并为一次发送，减少系统调用次数。

### 日志级别修复

热路径中的 INFO 日志改为 DBG，消除每 worker ~3400 条日志的 I/O 开销。

### 性能对比 (O2, golden_n50_sd9)

| 指标 | Before | After | 提升 |
|------|--------|-------|------|
| Wall Clock | 3502ms | 3105ms | -11.3% |
| t_total | 5.6ms | 4.3ms | -23.2% |
| read_nb | 3.4ms | 2.2ms | -35.3% |
| write | 1.0ms | 0.92ms | -8% |

---

## 2026-06-19: temp cache 重构 + 读重试策略 + wait_obj timeout + QA 拆分

**temp cache 重构**: `LocalObjectInfo::temp_compressed_data_` 从 `CMString` 改为 `FlyBufferPtr`。`write_temp_pickle` 直接压缩到 `FlyBufferPtr`，`put_temp_data` → `on_temp_write` 全链路 shared_ptr 透传，写入零拷贝。读取时直接返回 shared_ptr，读取零拷贝。`try_read_local_raw` 统一返回 `FlyBufferPtr`，temp 和非 temp 路径一致。仅淘汰到 `temp_eviction_store_` 时拷贝一次（低频路径）。

**put_temp_data 时序修复**: `on_temp_write`（存储数据到 `temp_compressed_data_`）移到 `register_write` 之前。`register_write` 是同步阻塞的，Master 收到 ACK 后立刻调度依赖任务，如果数据还未存储，其他 worker 读取会失败。

**读重试策略**: `read_raw_compressed` 远程回调改为单次尝试，移除 50ms×30s 重试循环。retry loop 是 workaround 而非正确修复——真正的问题是 Master Tier 3 回调未正确返回 `can_still_produce` 状态。

**can_still_produce 修复**: Master 的 `remote_compressed_read_handler` 回调在数据未找到时检查是否有 pending/running task，返回正确的 `can_still_produce` 状态。旧代码直接返回 `false`，导致 `wait_obj` 误判数据无法产出而报错。

**solver 竞态条件修复**: `ras_graph_check` 中 cleanup 从 `step-1` 改为 `step-2`。原代码中 step N 的 check 删除 `conv_{i}_{N-1}`，但 step N-1 的 check 可能还在读取这些数据（两者依赖不同，可并行执行）。

**依赖图日志**: `dependency_graph.cpp` 添加 INFO 级别日志，追踪 task 依赖注册和 ready 状态变化。task 模块新增 log 依赖（`dynamic_deps`）。

**Master 自读 race 修复**: `remote_compressed_read_handler` 改为直接查 `remote_idx` + `DataClient::request_compressed_data`，不再走 reactor 自查询。原路径经过 epoll，与 worker WriteRegister 在不同 fd 上的处理顺序不保证。

**wait_obj timeout**: `@wait_obj(timeout=30)` 新增可选超时参数。默认 `None` = 永远等待，直到数据可读或确认无法产出（`can_still_produce=false` 确认 3 次）。

**QA 测试拆分**: `test_read_cache.py` 拆为三个独立文件（`test_read_cache_basic.py`、`test_read_cache_cross_db.py`、`test_read_cache_large_objects.py`）。原文件中三个测试函数共享同一 fly 进程，`completed_tasks` 累积导致后续测试的 `wait_for` 被前面的残留数据欺骗。拆分后每个文件由 `runqa` 独立调度，`completed_tasks` 从零开始，保持直接 `read_object` 测试边界条件。

**fly wrapper 修复**: `build/bin/fly` wrapper 脚本设置 `FLY_BUILD` 环境变量，使 `fly.bin` 能自动定位 `build/` 布局，无需用户手动设置。big_qa 测试脚本同步修复环境变量设置。

---

## 2026-06-19: temp 写入路径优化 — 消除 C++→Python→C++ 往返

**问题**: Python `_write_temp` 先调 `_compress_pickle_bytes`（C++ 压缩 → 返回 Python bytes），再调 `_put_temp_data`（Python bytes → C++ CMString）。压缩结果经历 C++→Python→C++ 两次无意义拷贝。

**修复**: 新增 `Database::write_temp_pickle`（C++ 侧一步完成压缩+注册+存储），nanobind 绑定 `_write_temp_pickle`，Python `_write_temp` 直接调用。`storage_export.cpp` 新增绑定。

| 文档 | 变更 |
|------|------|
| docs/storage/module.md | 补充 temp 写入流程说明 |

---

## 2026-06-19: 写入时序重构 + 读写公共路径统一

**写入时序**：`write_object` 统一为 序列化+压缩 → put_low（cache）→ 注册（通知 master）→ 落盘。原时序中注册在序列化之前，master 标记数据就绪时 cache 未填充，其他 worker 读返回 `DATA_NOT_READY` 需重试。

**commit_write 提取**：`write_pickle_bytes`（Python pickle）和 `write_object<T>`（C++ 流式序列化）共享相同的 cache→register→enqueue 逻辑，提取为 `commit_write` 私有方法，净减 56 行。

**读侧公共路径**：两条读路径（Python `_read_decompressed` 和 C++ `read_object<T>`）都经过 `read_object_compressed` 作为公共 IO+缓存+backup 逻辑，无需额外提取。

**读重试参数**：`read_raw_compressed` 远程回调重试从 3 次×1s 改为 50ms×30s。

| 文档 | 变更 |
|------|------|
| docs/storage/module.md | 读路径说明补充 read_object_compressed 公共路径；写入流程更新时序 |
| docs/architecture.md | 写入流程 6 步更新；读重试参数更新 |

---

## 2026-06-19: internal task 判定 + worker config 共享 + DB 路径统一

- `TaskCompleteMessage` 新增 `is_internal_` 字段，替代 `task_id >= 100000` 脆弱判定
- worker 启动共享一份 config 文件（`.fly_config`），不再为每个 worker 创建独立文件
- QA 测试 DB 路径统一到 `log_dir/db`，coordinator 通过 `FLY_DB_PATH`/`FLY_DB_DIR` 环境变量传递共享路径给 helper

---

## 2026-06-19: QA 路径清理 + 分类整理 + 源码 bug 修复

- 删除 98 个 case/helper/script 的冗余 `sys.path.insert`（fly 已自动配好路径）
- 10 个多阶段测试改用 `get_fly_binary()`，消除 `bazel-bin` 硬编码
- `qa/internal/` 7 个 case 按内容拆分到 backup/storage/dependency/fault/unit/
- 源码修复：`read_object_compressed` cache 命中补 backup 检查；`request_db_path` 传 `existing_db_id`；`_update_latest_symlink` 用 `remove_all`

---

## 2026-06-18: 读写路径零拷贝优化 review 修复 — cache 语义 + xsputn 边界

**原因**: review `117c725`（读写路径零拷贝优化）发现三个问题：
1. `cache="none"` 未实现 — `read_object_compressed` 无条件查 low 层，`cache="none"` 仍命中缓存
2. `xsputn` 边界条件 — `buffer_.size() >= chunk_size_` 时 `space<=0`，`to_write<=0`，逻辑不够健壮
3. high-tier 语义回归 — `117c725` 把 `read_object<T>` 改成仅 `cache="high"` 时查/填 high 层，与 `_read_from_db` 设计初衷（C++ class 总是省反序列化）冲突，导致 `test_cpp_object_cache.py` 失败

**修复**:
- `read_object_compressed` 新增 `bypass_cache` 参数，`cache="none"` 时传 `true` 跳过 low 层查询
- `read_object<T>` 恢复重构前语义：`cache="low"`/`"high"` 都查+填 high 层，仅 `cache="none"` 完全 bypass
- `xsputn` flush 检查移到 insert 前，保证 `space` 恒正、`written` 恒前进
- 修正 `117c725` 新增的两个矛盾单测（`ReadObjectLowCacheDoesNotPopulateHighTier` → `ReadObjectLowCachePopulatesHighTier`）

| 文档 | 变更 |
|------|------|
| docs/storage/module.md | `read_object_compressed` 签名加 `bypass_cache` 参数；`read_object` cache 语义说明 |
| CLAUDE.md | object_cache.h 条目补 cache="none" bypass 说明 |

---

## 2026-06-17 (2): 零拷贝验证 — valgrind massif profiling 固化结论

**方法**: 10MB 对象远程传输（master + 2 worker），valgrind massif 追踪各进程堆分配树。

**结论**: wire 路径零用户态 copy。massif 分配树中无 DataResponseProtocol/MessageProtocol/FlyBuffer→CMString/substr/take 相关 heap 分配。worker 峰值分配全在 compress_pickle_bytes（写入压缩）+ decompress_raw_data（解压），均为序列化/压缩/解压的必然成本。

仅剩不可消除 copy: 内核 send/recv（syscall 固有）+ pickle 序列化/反序列化 + lz4 压缩/解压。

---

## 2026-06-17: Wire 协议优化 — DataResponse 分段传输 + string_view 零拷贝 header

**原因**: DataResponseMessage 的 compressed_data_（大对象压缩字节）原经 bitsery 序列化，5 次用户态 copy（500MB/100MB 对象）。改为两段传输（小字段 bitsery + raw payload 独立），消除全部用户态 copy。ObjectHeader::deserialize 改 string_view，消除 header 解析的全量 copy。

| 文档 | 变更 |
|------|------|
| docs/network/module.md | MessageProtocol 帧格式段新增 DataResponseProtocol 两段帧描述 + 方法表 |

改动:
- message_types.h: DataResponseMessage 移除 compressed_data_ 字段（改为传输层 raw payload）
- message_protocol.h: 新增 DataResponseProtocol（两段编解码 + parse_sub_header + raw_len_from_total）
- data_server: serve 用 DataResponseProtocol::encode + SendTask 携 raw_data；do_send 两段发送
- data_client/data_client_pool: 分步 recv（header + sub-header + small_fields + raw 直接进 FlyBuffer）
- object_header.h/cpp: deserialize 改 string_view（CMString/FlyBuffer/raw ptr 零拷贝传参）

消除 copy: wire 用户态 5 次 → 0 次（仅剩内核 send/recv）；header 解析全量 copy → 零拷贝。

---

## 2026-06-16 (4): FlyBufferPtr 全链路零拷贝重构 + write-through

**原因**: ObjectCache low 层原存 CMString，write/read 链路多次 copy。改为存 FlyBufferPtr（shared_ptr 共享所有权），缓存与读取链路全程零拷贝。write_object/write_pickle_bytes 落盘后 write-through 填入 low 层，立即启用数据可读性。

| 文档 | 变更 |
|------|------|
| CLAUDE.md | object_cache.h low 层描述 CMString→FlyBufferPtr；补 write-through 填入 |
| docs/architecture.md | 读缓存分层表 low 层 CMString→FlyBufferPtr；补 write-through |

改动（13 签名）:
- fly_buffer.h: 新增 FlyBufferPtr 别名
- object_cache.h: put_low/get_low 改 FlyBufferPtr（low 层 std::any 持 shared_ptr）
- data_reader/data_service: read_raw_bytes/try_read_local_raw/read_raw_compressed 等返回 FlyBufferPtr
- database: read_object_compressed 返回 pair<FlyBufferPtr, CMString>
- 回调 typedef + data_client/data_client_pool + worker/master agent: 返回 FlyBufferPtr
- data_server: wire egress FlyBufferPtr→CMString copy（wire 固有）
- write 路径: complete_ lambda 直接传 record（FlyBufferPtr），省 FlyBuffer→CMString copy

消除 copy: write→put_low（2次→0）、read→get_low→返回（1次→0）、DataServer serve 缓存命中（get_low copy→shared_ptr data()）。
保留 copy: wire encode/decode（序列化固有）。

---

## 2026-06-16 (3): 远程读复用 low 层缓存 + hit stats + remove 缓存清理补全

**原因**: ObjectCache low 层存的压缩字节正是远程传输载荷，可用于加速远程 DataServer 服务（省磁盘 IO）。补充 hit stats 诊断 + 修复远程 remove 场景的缓存失效 gap。

| 文档 | 变更 |
|------|------|
| docs/architecture.md | 读缓存分层表 low 层「服务对象」补远程路径（DataServer try_read_local_raw short-circuit）；失效路径补 remove_local_index/remove_remote_index；新增命中统计行 |

改动:
- data_service.cpp try_read_local_raw: 入口 short-circuit（命中 low 层省磁盘 IO）+ 磁盘读后 put_low
- object_cache.h: Stats 结构（per-tier hits/misses/puts/evictions，atomic）+ low_hit_rate/high_hit_rate
- storage_export.cpp: ex_stg_cache_stats Python 绑定
- data_service.cpp remove_local_index/remove_remote_index: 补 ObjectCache::remove（修复远程 remove 广播的陈旧缓存 bug）

---

## 2026-06-16 (2): write API 返回 WriteErrorType 错误码（独立于 TaskErrorType）

**原因**: write_object/write_pickle_bytes 原返回 CMString（成功失败都为空），Python wrapper 靠 task 级累积的 last_error_type 区分成败，导致跨测试 error_type 污染（生产 bug）。改为返回独立 WriteErrorType 错误码，per-call 明确区分。

新增 `WriteErrorType` 枚举（src/common/cpp/error_types.h）：OK/FROZEN_DB/REGISTRATION_FAILED/REGISTRATION_TIMEOUT/DUPLICATE_SKIPPED。不复用 TaskErrorType（那是 task 执行级累积状态，worker_agent task 失败检测依赖）。

| 改动 | 说明 |
|------|------|
| database.h/cpp | write_object/write_pickle_bytes 返回 WriteErrorType（原 CMString）|
| export_macros.h / storage_export.cpp | _write_to_db / _write_pickle_bytes / write_object_raw 返回 int；导出 EXStgWriteErrorType 枚举 |
| database.py | write_object 据 return code 判断（OK/DUPLICATE_SKIPPED = 成功），删除 last_error_type 快照逻辑 |
| last_error_type | 保持 task 级累积语义不变（write_object 仍设它供 worker_agent task 失败检测）|

无文档需更新（Python write_object 签名不变；agent/module.md 的 last_error_type 描述仍准确）。

---

## 2026-06-16: C++ ObjectCache — 两层 LRU 读缓存（low 层下沉 C++ + nanobind high 层）

**原因**: 新增 C++ 侧 read 缓存系统，统一 master/worker 进程的读缓存。low 层（压缩字节）下沉 C++，消除 Python 与 C++ 的双缓存冗余；high 层（反序列化对象）C++ 服务 read_object<T> + nanobind 类（经 _read_from_db），Python 服务 pickle 对象。

| 文档 | 变更 |
|------|------|
| CLAUDE.md | 存储层文件表新增 object_cache.h 行（两层 LRU + std::any + LFU 淘汰） |
| docs/python-api/module.md | read_cache.py 描述更新（low 下沉 C++）；read_object cache 语义补充分层 |

新增文件: `src/storage/cpp/object_cache.h`（header-only，进程级单例）、`src/storage/tests/object_cache_test.cpp`（16 单测）、`qa/test_cpp_object_cache.py`（6 case，含 high 层命中断言）。
集成: `read_object_compressed` low 层（命中省 IO）、`read_object<T>` high 层（命中省反序列化）、`remove_object`/`remove_index_entry` 失效。
nanobind: `FLY_EXPORT_SERIALIZE` 加 `_read_from_db`（对称 `_write_to_db`）；`_get_py_name` 辅助分派；database.py read_object 据类型分流（nanobind→C++ high / pickle→Python high）。
诊断: `ex_stg_cache_high_size` / `ex_stg_cache_clear`（测试/观测用）。

---

## 2026-06-15 (2): connect 失败非致命化 — 返回 0 sentinel，不抛异常

**原因**: 8606397 网络层重构后 `connect()` 失败抛异常，把连接失败当致命错误，破坏 worker_agent_test（无 master 场景）及「连接失败非致命」契约。改为返回 sentinel 让调用层判断。

| 文档 | 变更 |
|------|------|
| CLAUDE.md | 网络层表格 connection_manager.h 补 connect() 失败返回 0 语义 |
| docs/ARCHITECTURE_REVIEW.md | §3.2 从「待修复（建议 throw）」改为「已处理 — 方向调整（返回 sentinel 不抛）」，记录设计决策 |

新行为：`connect()` 失败返回 0（conn_id 从 1 起，0 = 失败），不抛异常；`WorkerAgent::start()` 检测 0 后终止（不进入存活未注册态）。

---

## 2026-06-15: db_id 生成策略重构（UUID v4 → path-hash + 随机）

**原因**: db_id 从 UUID v4（32 hex）改为 10-char base62（4 char path-hash 前缀 + 6 char 随机后缀）。同路径 → 同前缀，使路径迁移后 load 旧 db + 原路径新建的碰撞可被检测。

| 文档 | 变更 |
|------|------|
| docs/python-api/module.md | §open_db 路径检测 db_id 格式说明（UUID v4 → 10-char base62）；db_id 生成段重写（path-hash 前缀 + 随机后缀 + 碰撞检测） |

---

## 2026-06-01: 用户脚本 task 支持 + 两层读缓存

| 文档 | 变更 |
|------|------|
| docs/python-api/module.md | `as_task` 实现描述更新（from_user 协议）；`read_object` 新增 `cache` 参数 |
| qa/README.md | 新增"扁平脚本"编写规范，移除 `__main__`/`main()` 模板 |

新功能：
- **用户脚本 task**: `@as_task()` 装饰器自动检测 `__main__` 模块，将函数 cloudpickle 序列化到 task_name 字段（`from_user` 协议），Worker 端反序列化重建函数执行。用户脚本无需特殊结构。
- **两层读缓存**: `read_object(name, cache="low"|"high"|"none")` — LOW 层缓存压缩数据（避免网络/磁盘 IO），HIGH 层缓存反序列化对象（避免重复 pickle.loads）。LRU + 读取频率淘汰，30s 保护期，1.5x 硬限制。缓存大小由 `read_cache_size` config 控制（默认 1GB）。
- **save_to_db=False**: `write_object(name, obj, save_to_db=False)` 将压缩数据存入 `local_idx_` 的 `temp_compressed_data` 内存字段（不落盘到 DB 文件）。LRU 淘汰溢出到 TempStore 临时文件。`read_object` 通过 `local_idx_` 统一路径透明访问（支持跨 Worker，通过 WriteRegister 通知 master）。`remove_object` 清理 temp 条目。freeze/析构时自动清理。
- **Master 自动启动**: `init()` 中自动调用 `agent.start()`，用户无需手动启动 Master。
- **QA 公共 API 规范**: 所有 QA 测试只使用公共 Python API（`master.worker_count`、`master.wait_for_workers()` 等），底层 C++ 测试迁移至 `qa/internal/`。每个测试文件只测试一个场景。
- **QA 扁平脚本**: 所有 QA 测试移除 `if __name__ == "__main__":`、`main()`、`master.stop()`、`del db`。代码从上到下直接执行。
- **Master 公共 API**: 新增 `worker_count`、`wait_for_workers(n, timeout)`、`is_running()`、`get_worker_pids()`。

---

## 2026-05-31: Code Review Fixes — API 安全性改进

| 文档 | 变更 |
|------|------|
| docs/task/module.md | `get_worker()` / `get_task()` 返回类型从指针改为 `std::optional<std::reference_wrapper<T>>` |
| docs/agent/module.md | `set_data_service(DataService*)` → `set_data_service(CMWeakPtr<DataService>)`；`WorkerAgentContext` 回调从 C 函数指针 + trampoline 改为 `std::function` + lambda；移除所有 trampoline 声明；新增 `set_notify_removed_func`/`set_remove_request_func`/`set_backup_request_func`；`DataService` 继承 `enable_shared_from_this`，新增 `instance_ptr()` |
| docs/storage/module.md | 回调模式从 C 函数指针更新为 `std::function`；`DataService` 继承 `enable_shared_from_this`；设计决策表更新 |
| docs/network/module.md | `register_handler` 优化说明（decode() 已 in-place 修改 buffer，无需额外拷贝） |

核心变更：
- `WorkerManager::get_worker()` 返回 `std::optional<std::reference_wrapper<WorkerInfo>>` 替代 `WorkerInfo*`
- `TaskManager::get_task()` 返回 `std::optional<std::reference_wrapper<TaskMetadata>>` 替代 `TaskMetadata*`
- `WorkerAgentContext` 所有回调改用 `std::function`，移除 `void*` ctx 和全部 7 个 trampoline 静态函数
- `DataService` 继承 `std::enable_shared_from_this<DataService>`，`instance_ptr()` 返回 `CMSharedPtr<DataService>`；Agent 通过 `CMWeakPtr<DataService>` 观察
- `Config::INVALID_INT`（`INT64_MIN`）替代未知 key 抛异常；`get_int()` 使用 `fprintf(stderr, ...)` 日志（core 模块无 log 依赖）
- `TaskExecutor::cancel()` 移除（header、cpp、test、export）
- Reactor `register_handler` 移除冗余 buffer 拷贝（`decode()` 已 in-place 修改）
- `main.py` triple `gc.collect()` → 单次调用

---

## 2026-05-30: 统一流式序列化+压缩管线重构

| 文档 | 变更 |
|------|------|
| docs/storage/module.md | 全面重写：Database API（删除 write_object/write_object_typed/write_object_buffer/read_object 非模板版，新增 write_object_raw_ptr/read_object_compressed）；DataWriter 简化为纯落盘；DataReader 简化为纯读字节；IndexEntry 删除 compression_type 字段和版本控制；写入/读取流程更新为流式管线架构；新增序列化宏说明 |
| CLAUDE.md | FLY_SERIALIZE 说明更新为自动生成 fly_serialize/fly_deserialize |
| AGENTS.md | storage 模块描述更新，新增 CompressingStreamBuf/DecompressingStreamBuf |
| src/main/cpp/BUILD | 所有导出 .so 加入 data 依赖，修复 bazel clean 后 .so 不重建问题 |

核心变更：
- DataWriter 移除所有压缩配置和逻辑，只保留 write_record 纯落盘
- DataReader 移除所有解压逻辑，只保留 read_raw_bytes + exists
- Database 统一管理 compress_data_to_buffer（写）和 DecompressingStreamBuf（读）
- FLY_STREAMABLE() 宏合并进 FLY_SERIALIZE_END，所有 FLY_SERIALIZE 类型自动获得流式能力
- FLY_EXPORT_SERIALIZE 合并 _write_to_db + is_cpp + __getstate__/__setstate__
- write_record 删除 compression_type 参数，IndexEntry 删除 compression_type 字段
- IndexEntry 删除版本控制（FLY_SERIALIZE_BEGIN → FLY_SERIALIZE）
- Python database.py 简化为两条写路径：_write_to_db / pickle
- 删除 write_object(name, data) / read_object(name) / write_object_typed / write_object_buffer

---

## 2026-05-30: backup 数据复制 — 压缩传输零解压落盘

| 文档 | 变更 |
|------|------|
| docs/architecture/overview.md | 数据副本策略从"低/未实现"更新为"已完成：backup=True 压缩传输零解压落盘" |
| docs/python-api/module.md | write_object/read_object/write_object_raw/read_object_raw 签名新增 backup=False 参数说明 |
| docs/storage/module.md | read_object/read_object_typed 签名新增 backup=false；新增 backup_object() 声明 |

---

## 2026-05-25: 优雅关机 + workers_mutex_ 线程安全

| 文档 | 变更 |
|------|------|
| docs/agent/module.md | 关机流程重写为"优雅关机（Graceful Shutdown）"：drain 语义、pending task 持久化、stop 幂等；MasterAgent 成员变量新增 draining_/shutdown_requested_/fatal_error_/workers_mutex_/drain_mutex_/drain_cv_/drain_thread_；WorkerAgent 新增 shutdown_triggered_；schedule_tasks 新增 draining early return；on_disconnect 新增 draining 跳过恢复逻辑；设计决策表新增 workers_mutex_/stop 幂等/SIGTERM Python 层处理 |
| CLAUDE.md | 新增 Agent 工作指南 §7 禁止项：禁止归因为 pre-existing bug、禁止忽略 crash/不稳定性、崩溃与不稳定性零容忍 |

代码变更摘要：
- `master_agent.h/cpp`: stop() 改为 drain 语义（广播 shutdown → 等待 running tasks → persist pending → cleanup）；schedule_tasks() draining early return；on_task_failed 设 fatal_error_；on_disconnect draining 跳过恢复；新增 workers_mutex_ 保护 conn_to_worker_/worker_to_conn_ 全部并发访问（修复 SIGSEGV）；新增 persist_pending_tasks()、build_failed_record()、notify_drain_if_active()、check_shutdown_request()（dead code）、do_drain_and_stop()
- `worker_agent.h/cpp`: initiate_shutdown() 幂等（shutdown_triggered_）；stop() → initiate_shutdown() → do_cleanup()
- `reactor.h`: 新增 is_running()、get_io_pool()
- `data_service.h/cpp`: stop_transfer_server() 中 reset transfer_pool_；新增 reset() 公共方法
- `main.py`: SIGTERM handler → SystemExit(0) → cleanup
- `master_agent_test.cpp`: 4 new tests (StopWithPendingTasks, StopNoRunningTasks, StopIdempotent, StopBeforeStart)
- `worker_agent_test.cpp`: 1 new test (InitiateShutdownFromOnDisconnect)
- qa/: 5 new files (test_graceful_shutdown, test_shutdown_broadcast, test_pending_task_persist + 2 helpers)

---

## 2026-05-25: FlyBuffer 统一 + 流式管线架构重构

| 文档 | 变更 |
|------|------|
| docs/storage/module.md | 写入流程改为"流式管线 + 异步落盘"架构；新增 FlyBufferStreamBuf/CountingStreamBuf 组件描述；DataWriter 新增 compress_to_buffer/write_record |
| CLAUDE.md | 存储层文件表更新（database/data_writer/fly_buffer_stream）；序列化部分新增 FlyBuffer 说明；写入架构约束更新 |

代码变更摘要：
- `fly_buffer.h`: FlyBuffer 内部存储从 `CMVector<uint8_t>` 改为 `CMString`，消除 char↔uint8_t 阻抗失配；新增 `take(CMString&&)` / `release()` 支持零拷贝
- `serialization_macros.h`: FlySerBuf 改为 FlyBuffer 别名；FLY_ENCODE/DECODE 去掉 std::transform 转换；新增 bitsery traits 特化
- `fly_buffer_stream.h`（新建）: FlyBufferStreamBuf（streambuf→FlyBuffer）+ CountingStreamBuf（字节计数）
- `data_writer.h/cpp`: 新增 `compress_to_buffer`（流式管线：FlyBufferStreamBuf→CompressingStreamBuf→FlyBuffer）和 `write_record`（仅 file_stream_.write + index 更新）
- `database.h/cpp`: write_object 模板改为调用线程 serialize+compress → WBQ 仅 write_record；新增 write_object_raw_ptr 接受裸指针
- `export_macros.h`: `__getstate_buffer__` 改用 FLY_ENCODE_TO_BUFFER 直接写入 FlyBuffer
- `storage_export.cpp`: 新增 `_write_pickle_bytes`（Python bytes 裸指针直接进 compress_to_buffer）和 `_write_raw_ptr`
- `database.py`: Python pickle 路径改用 pickle.dumps + _write_pickle_bytes
- `data_reader.cpp`: 读取路径 `FlySerBuf(str.begin(), str.end())` 改为 `take(std::move(str))` 零拷贝

---

## 2026-05-25: DataService 两层索引重构 + 并发 Bug 修复

| 文档 | 变更 |
|------|------|
| docs/storage/module.md | `local_idx_`/`remote_idx_` 类型从 `Map<full_name, ...>` 改为两层嵌套 `Map<db_id, Map<short_name, ...>>`；新增 `split_full()` 定长切分（32 字符 db_id） |

代码变更摘要：
- `data_service.h/cpp`: `local_idx_`/`remote_idx_` 改为两层索引；`split_full()` 使用 32 字符定长切分；`on_flush(db_id)` 优化为 O(该 db 条目)
- `local_index.h/cpp`: 加 `std::mutex` 保护所有公共方法；`save()` 锁内取快照锁外做 I/O（修复 WBQ 线程与主线程数据竞争导致的 std::bad_alloc）
- `tcp_transport.cpp`: `send()` 处理 partial send 和 EAGAIN（poll+retry 循环）
- `worker_agent.cpp`: `on_remove_command` 提取 short name（修复 double-prefix）
- `database.cpp`: `freeze()` 不再关闭 DataWriter（修复 in-flight write 竞争）
- `main.cpp`: 新增 `std::set_terminate()` crash handler + SIGABRT/SIGSEGV handler + backtrace
- 4 个测试文件改为使用 32 字符 db_id

---

## 2026-05-24 (7): Bug 修复 + 压力测试 + freeze 机制完善

| 文档 | 变更 |
|------|------|
| CLAUDE.md | 新增 §QA 测试与 test 模块；新增 §内部接口；test 模块描述；消息结构数量 26→27 |
| qa/README.md | 新增 §6 压力测试（7 覆盖场景 + 2 未覆盖场景） |
| docs/test/module.md | 新增 `increment`、`write_after_freeze` 任务文档 |
| docs/network/module.md | 消息结构数量 26→27；新增 `DatabaseFreezeNotification` |
| docs/architecture.md | 消息结构数量 23→27 |
| docs/architecture/overview.md | 消息类型总览 22→26+header |
| docs/DOC_CHANGELOG.md | 本条记录 |

---

## 2026-05-23 (5): writer_id UUID 解耦 idx/data 文件命名

**原因**: load_db 时 worker_id 与 idx 文件名耦合导致冲突限制，Master 无法在任意机器上重启

| 模块文档 | 主要变更 |
|----------|----------|
| CLAUDE.md | §13 load_db 流程重写（Phase 0 冲突检查移除、Phase 2 不加载 idx、Phase 3 hostname 复用、Phase 4 含 Master writer_id）；§8 MasterAgent/WorkerAgent 模块描述更新；_DB_META WorkerInfo 新增 writer_id 字段 |
| docs/storage/module.md | DataWriter/DataReader 构造函数参数 `uint64_t worker_id` → `const CMString& writer_id`；私有成员 `worker_id_` → `writer_id_` |
| docs/agent/module.md | `restore_master_idx`/`send_idx_load_commands` 签名更新（writer_ids）；load_db 流程重写（Phase 2-5 全部更新） |
| docs/network/module.md | `IdxLoadCommandMessage.old_worker_ids` → `writer_ids` (CMVector\<CMString\>) |

**代码变更摘要**：
- `writer_id`: 8-char hex UUID，Database 构造时生成，用于 idx/data 文件命名
- 文件命名：`{writer_id}.idx`、`data_{writer_id}_{index:03}.dat`（替代 `worker_{id}.idx`、`aggregated_w{id}_*.dat`）
- `DataReadyMessage` 新增 `writer_id` 字段，Worker/Master 均填充
- `IdxLoadCommandMessage.old_worker_ids` → `writer_ids`
- `rebuild_remote_idx`：统一路径，worker_id==0 不再特殊处理，所有条目按 hostname 映射到新 Worker
- Master load_db 时不加载任何 idx 到 local_idx，所有旧数据通过 remote_idx 经 Worker 提供
- `MasterAgent` 新增 `get_worker_hostnames()`、`add_worker_hostname()`
- `register_worker(0, host_, port_)` 在 `start()` 中调用（Master 新数据仍需被 Worker 读取）
- `recorded_workers_` key 从 `pair<hostname, worker_id>` 改为 `tuple<db_id, hostname, writer_id>`
- agent.py load_db 重写：hostname-based worker 复用，Master writer_id 含入 idx load commands
- 新增 3 个多 hostname 单元测试覆盖 idx 分配场景
- 3 个网络测试 flaky 修复（poll loop 替代 sleep+assert）

---

## 2026-05-23 (4): load_db 增强 + 跨 DB QA 测试

**原因**: 连续 load_db 多个 DB 时 `_next_worker_id` 回退导致 worker ID 冲突；load_db 恢复后数据未标记依赖就绪

| 模块文档 | 主要变更 |
|----------|----------|
| CLAUDE.md | §load_db 完整流程增加 Phase 0 冲突检查、Phase 2 mark_data_ready + frozen 恢复、连续 load_db 说明 |

**代码变更摘要**：
- `agent.py` `load_db`: 新增 worker ID 冲突检查（重叠 → RuntimeError）；`_next_worker_id` 取 `max(当前, max(old)+1)` 不回退
- `database.cpp`: 构造函数检测 `_FROZEN` 标记恢复 `is_frozen_` 状态
- `master_agent.cpp`: `recorded_workers_` 从 `pair<hostname,worker_id>` 改为 `tuple<db_id,hostname,worker_id>`（per-DB 记录）；`restore_master_idx` + `rebuild_remote_idx` 新增 `mark_data_ready`
- 新增 6 个跨 DB e2e_tasks：cross_db_copy, cross_db_sum, add_alpha_property, alpha_cross_db_copy, gpu_cross_db_copy, triple_db_sum
- 新增 QA 测试 test_complex_scenario.py：2 进程协调器，覆盖多 DB、跨 DB 依赖、load_db 双 DB 迁移、动态属性、restart_failed_tasks、triple-DB 计算（12 个验证点）

---

## 2026-05-23 (3): 异步写入依赖调度重构

**原因**: `write_object` 异步写入时立即触发依赖满足，移除 `restart_failed_tasks` 中的 `drain_write_back` 同步阻塞

| 模块文档 | 主要变更 |
|----------|----------|
| CLAUDE.md | 重写 §restart_failed_tasks API（简化流程）、重写 §写入注册触发依赖满足（Worker/Master 端分离、线程安全） |
| docs/agent/module.md | WriteRegisterMessage 语义变更（增加 mark_data_ready + update_remote_idx） |

**代码变更摘要**：
- `on_write_register`: 收到 Worker WriteRegisterMessage 后立即 `mark_data_ready` + `update_remote_idx` + `schedule_tasks`
- `setup_write_context`: Master 端新增 `master_register_write_trampoline`，`write_object` 时同步触发 `mark_data_ready`
- `restart_failed_tasks`: 移除 `drain_write_back` + 手动依赖检查，简化为直接 `submit_task`
- `schedule_tasks`: 新增 `schedule_mutex_` 防止 WriteBackQueue 工作线程与 Python 线程并发导致重复 fail/persist

---

## 2026-05-23 (2): db_id UUID v4 + open_db 路径递增

**原因**: db_id 从 hash(base_path) 改为 UUID v4；open_db 检测已有 DB 时自动递增路径

| 模块文档 | 主要变更 |
|----------|----------|
| CLAUDE.md | 新增 §open_db vs load_db 路径检测表（递增路径）、§db_id 生成（UUID v4）、DataService db_paths_ register/unregister 行为 |
| docs/python-api/module.md | 新增 §open_db 路径检测（自动递增 `.1` `.2`... + WARN）、db_id UUID v4 说明 |
| docs/storage/module.md | Database 构造函数注释（UUID v4、析构 unregister）、DataService register_database 严格检查注释 |

---

## 2026-05-23 (1): load_db 文档同步

**原因**: load_db 功能实现完成后，同步更新所有相关模块文档

### 变更汇总

| 模块文档 | 主要变更 |
|----------|----------|
| storage/module.md | Database 构造函数新增 `existing_db_id`，DataService 新增 `has_database()`、`restore_entries()`、`DbPaths` struct |
| agent/module.md | MasterAgent 新增 `restore_master_idx()`、`send_idx_load_commands()`、`rebuild_remote_idx()`、hostname 映射、`on_idx_load_ack()` handler；WorkerAgent 新增 `on_idx_load_command()` handler；新增 load_db 恢复流程文档 |
| network/module.md | 消息类型 24→26 种；RegisterMessage 新增 `hostname`、`ip_address` 字段；新增 `IdxLoadCommandMessage`(type=25)、`IdxLoadAckMessage`(type=26) |
| python-api/module.md | FlyAgent 新增 `load_db()`、`wait_for_all_workers()`；`_deserialize_args` 增加 `has_database` 检查说明；新增 load_db 使用示例 |
| superpowers/plans/2026-05-22-* | DbMetaHeader/DbMeta 移除 `base_path`；Phase 3 改为 process worker；Phase 4 增加 `register_database` 步骤 |
| CLAUDE.md | §8 DataService (db_paths_, has_database)、§13 load_db 流程更新（process worker, base_path 移除） |

---

**日期**: 2026-05-21
**原因**: 文档与实现代码存在大量不一致，本次批量修正

---

## 一、变更汇总

| 模块 | 差异数 | 主要变更 |
|------|--------|----------|
| log | 18 | 架构从多实例改为单例，API完全重写 |
| network | 28 | 新增MasterClient，TransportLayer/Reactor签名变更 |
| task | 40 | WorkerInfo字段重设计，TaskScheduler签名变更 |
| storage | 22 | 写流程改为异步WriteBackQueue，DataReader实例化 |
| agent | 23 | WorkerAgentContext从指针改为回调模式 |
| core | 7 | 新增8个int+6个string配置项 |

---

## 二、详细变更

### 2.1 log/module.md — 架构完全重写

**旧设计（文档）**:
- 多实例模式：`CMMap<CMString, Logger>` 存储多个Logger
- `get_master()`, `get_worker(worker_id)` 分角色获取
- `init_master(path)`, `init_worker(worker_id, path)` 分角色初始化
- `debug(component, msg)` 两参数日志方法
- 日志格式：`[timestamp] [LEVEL] [component] msg`

**新设计（实际代码）**:
- 单例模式：`static Logger* instance_`
- `Logger& instance()` 统一获取
- `init(base_dir, worker_id)` 统一初始化
- `debug(msg)` 单参数日志方法（无component）
- 日志格式：`[timestamp] [LEVEL] msg`
- 日志rotation：`resolve_log_dir()` 创建版本化目录 + `.latest` symlink
- 宏定义：`DBG(msg)`, `INFO(msg)`, `WARN(msg)`, `ERR(msg)`
- Python导出：`init_log`, `shutdown_log`, `flush_log`, `set_log_level`, `DBG/INFO/WARN/ERR`

---

### 2.2 network/module.md

**新增类**:
- `MetadataClient`（原名 `MasterClient`） — 阻塞TCP客户端，查询Master数据位置
  - `query_data_location(host, port, object_name)` → `DataLocation`

**签名变更**:
- `TransportLayer::accept()` → **已移除**，改为 `stop_listening()`
- `TransportLayer::get_bound_port()` 返回 `int` 而非 `int32_t`
- `Reactor` 构造函数：`CMUniquePtr` 而非 `std::unique_ptr`
- `Reactor::set_io_pool()`：`CMSharedPtr` 而非 `IOThreadPool*`
- `Reactor::recv_buffers_`, `handlers_`：`CMUnorderedMap` 而非 `CMMap`
- `IOThreadPool` 构造函数接收 `thread_count`，`start()` 无参数
- `DataClient::request_data()` 返回 `std::tuple<bool, CMString, CMString>` 而非 `DataResponse`

**新增Reactor方法**:
- `on_connect(handler)`, `on_disconnect(handler)`, `on_error(handler)`
- `run_once(timeout_ms)`, `get_bound_port()`, `connect(host, port)`

**消息类型修正**:
- 22种枚举值，但仅17种有对应struct（DATABASE_FREEZE等5种无struct）

---

### 2.3 task/module.md

**DependencyGraph变更**:
- `mark_data_ready()` 返回 `void` 而非 `CMVector<uint64_t>`
- 新增 `get_pending_tasks()`, `is_task_ready()`
- `has_task()` 已移除

**WorkerInfo重设计**:
- 旧：`role` (string), `attributes`, `is_busy` (bool), `last_heartbeat` (double)
- 新：`address`, `port`, `capabilities`, `status` (WorkerStatus enum), `last_heartbeat` (uint64_t)
- WorkerStatus枚举：`IDLE=0, BUSY=1, DEAD=2`

**WorkerManager签名变更**:
- `register_worker(id, address, port, capabilities)` 而非 `register_worker(id, role, attributes)`
- 新增：`unregister_worker()`, `update_worker_status()`, `assign_task()`, `get_workers_with_capability()`
- 移除：`has_worker()`, `get_available_workers()`, `get_all_worker_ids()`, `update_attributes()`

**TaskScheduler变更**:
- 构造函数：`DependencyGraph*`, `WorkerManager*` 原始指针而非shared_ptr
- `submit_task()` 已移除（任务提交在MasterAgent层）
- `schedule_next()` → `ScheduleResult`
- `schedule_all_available()` → `CMVector<ScheduleResult>`
- 新增 `ScheduleResult` 结构：`{task_id, worker_id, scheduled}`

**MetadataManager变更**:
- TaskStatus枚举：`PENDING=0, RUNNING=1, COMPLETED=2, FAILED=3, CANCELLED=4`（无READY）
- `create_task(id, name, inputs, outputs, config)` 新增outputs/config参数
- `get_task()` 返回指针而非值
- 新增字段：`outputs`, `config`, `created_at`, `started_at`, `completed_at`, `error_message`, `assigned_worker_id`

**HeartbeatMonitor变更**:
- 构造函数：`WorkerManager*` 原始指针 + `timeout` 参数
- 默认timeout：30秒而非120秒
- `check_all_workers(uint64_t)` 而非 `check_all_workers(double)`
- `set_timeout(uint64_t)` 而非 `set_timeout(double)`

---

### 2.4 storage/module.md

**Database变更**:
- 构造函数：`writer_id` 类型 `uint64_t` 而非 `int`，新增 `host` 参数
- `flush()` 不再作为公共方法（异步WriteBackQueue）
- 新增：`load_meta()`, `set_db_id()`, `reset()`
- 写流程：异步入队 `WriteBackQueue`，非阻塞返回

**DataWriter变更**:
- 构造函数：11个参数而非2个
- 新增：`close()`, `total_bytes_written()`, `file_count()`
- `get_last_entry(object_name)` 返回指针，需object_name参数

**DataReader变更**:
- 所有方法从 `static` 改为实例方法
- 构造函数：`DataReader(base_path, data_path, worker_id)`
- 新增：`exists()`, `read_object<T>()`

**DataService变更**:
- `try_read_local()` 返回 `std::pair<bool, ReadResult>` 而非 `ReadResult`
- `lookup_remote_idx()` 返回 `RemoteObjectInfo` 结构而非输出参数
- 新增：`register_database()`, `unregister_database()`, `has_local_object()`, `has_remote_location()`
- 新增：`start_transfer_server()`, `stop_transfer_server()`, `drain_write_back()`
- 移除：`process_completions()`

**IndexEntry变更**:
- 版本：3而非2
- `block_count`：`int32_t` 而非 `int`
- `compression_type`：`int8_t` 而非 `CompressionType` 枚举
- 新增 `host` 字段

**Compressor变更**:
- 从静态工具类改为虚接口
- 实例方法：`compress()`, `decompress()`, `compress_chunk()`, `decompress_chunk()`
- 工厂方法：`create()`, `create_from_name()`

---

### 2.5 agent/module.md

**WorkerAgentContext重构**:
- 旧：`set(WorkerAgent*)` 指针存储 + `current()` 获取
- 新：`set(RecordWriteFunc, void* ctx)` 函数指针回调 + `record_write()` 调用回调
- 文件位置：`src/common/cpp/worker_context.h` 而非 `src/agent/cpp/`

**MasterAgent变更**:
- `get_port()`：`uint16_t` 而非 `int`
- `get_pending/running/completed_tasks()`：返回 `CMVector<uint64_t>` 而非 `int`
- 新增：`set_data_service()`, `get_connected_workers()`, `register_database()`, `is_db_frozen()`, `request_remote_data()`, `request_data_from_worker()`
- 移除：`on_data_query()`（改为 `on_data_ready()`）

**WorkerAgent变更**:
- `poll_task()`：返回 `bool` 而非 `void`
- `request_data_from_worker()`, `request_remote_data()`：返回 `ReadResult` 而非 `DataResponse`
- 新增：`get_worker_id()`, `set_executor()`, `begin_task()`, `record_write()`, `end_task()`, `register_write_with_master()`
- 消息处理器：移除 `conn_id` 参数
- 移除：`on_data_location()` 处理器

**TaskExecutor变更**:
- TaskExecStatus枚举新增 `TIMEOUT=2`
- 新增：`clear_exec_func()`, `is_running()`

---

### 2.6 core/module.md

**新增int配置项**:
- `worker_mode`, `worker_id`, `compression_level`, `compression_threshold`, `compression_stream_chunk_size`, `dependency_update_mode`, `interactive`, `cli_master_port`

**新增string配置项**:
- `transport_type`, `compression_type`, `data_server_host`, `master_host`, `log_dir`, `script_path`

**值修正**:
- `large_file_threshold`：已修正为 `67108864`（64MB），新增 `large_file_threshold_kb`（65536，用户可配置 KB 单位）
- `database.cpp` 使用 `large_file_threshold_kb * 1024` 计算字节阈值

**Python导出修正**:
- 移除 `FLY_EXPORT_INIT()`（实际不存在）
- 新增：`mark_workers_launched`, `is_workers_launched`, `reset`
- `ex_core_get_config`：返回指针而非引用

---

## 三、文档修正状态

| 文档 | 状态 |
|------|------|
| log/module.md | 已修正 |
| storage/module.md | 已修正 |
| network/module.md | 已修正 |
| task/module.md | 已修正 |
| agent/module.md | 已修正 |
| core/module.md | 已修正 |
| architecture/overview.md | 已修正 |
| DEVELOPMENT_GUIDELINES.md | 无需修正 |
| superpowers/plans/*.md | 保留历史记录，不修正 |

---

## 四、代码修正建议

| 位置 | 问题 | 建议 |
|------|------|------|
| `config.cpp:66` | `large_file_threshold = 10485710` | **已修正**: 改为 64MB (`67108864`)，新增 `large_file_threshold_kb = 65536`，database.cpp 使用 `large_file_threshold_kb * 1024` |
| `master_client.h/cpp` | `MasterClient` 命名不准确 | **已修正**: 重命名为 `MetadataClient`，功能为元数据查询 |