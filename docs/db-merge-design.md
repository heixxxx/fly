# DB Merge 设计与实现方案

> 状态：**已实现**（v4 设计 + TDD 实现，2026-07-28）
> ⚠️ **部分被取代**：本文写作时的 db_id/_MIGRATED_TO 迁移追踪机制已被 [db-chain-design.md](db-chain-design.md)（2026-08-08，uid 链）取代——merge 语义改为「彻底删源 + target 继承 uid + 邻居边更新」。**merge 编排流程与消息仍以本文为准；迁移追踪与 db 身份以 db-chain-design.md 为准。**
> 制定日期：2026-07-22（v4 设计定稿）；实现完成：2026-07-28
> 关联：`docs/architecture.md` §3.3（双路径契约）、§5.3（freeze 后处理）、§5.4（backup）；`docs/adr/0001-db-meta-and-load-db.md`
>
> **实现状态**：
> - ✅ 消息类型 `DeleteDataMessage`/`DeleteDataAck`（msg=42/43）+ `MergeCleanupMessage`（msg=44）+ round-trip 测试
> - ✅ worker 端 `__merge_object` internal task（方案 B 独立 DataWriter）+ `on_delete_data` handler + `on_merge_cleanup` handler
> - ✅ master 端 `send_merge_task` / `send_delete_data` / `wait_merge_tasks_complete` + `on_delete_data_ack` + `cleanup_after_merge`
> - ✅ C++ 集成测试 `IdxLoadTest.MergeObjectEndToEnd`（master 派发 merge + delete + cleanup 全流程）
> - ✅ Python `Master.merge_db` 5 阶段编排（含前置 task 等待 + 阻塞语义）+ `fly.merge_db` 导出
> - ✅ merge 后状态清理：广播 MergeCleanup → 各 worker 清旧 local_idx/remote_idx + 按新路径 load idx 重建 local_idx（同 host/共享 FS 可本地直读 .dat）
> - ✅ QA 测试 `qa/storage/test_merge_db.py`（merge + 删源 + 清理）+ `test_merge_db_waits_for_tasks.py`（前置 task 等待限制）
> - ✅ 全量回归：50/50 C++ 单测 + 113/113 QA 通过

---

## 0. 摘要

提供 **`fly.merge_db(path, ...)` 主动 API**：用户显式调用，把分散在各源 host 本地 `data_path` 的 `.dat` 数据**通过网络集中到 master host**，产出一个 data 自包含、索引沿用源共享 `base_path` 的合并数据库。

**核心定位**（区别于 backup）：
- backup = 跨机**冗余副本**（容灾，多副本）
- merge = 跨机**数据集中**（聚合到单 host，源清理）

**五个已定决策**：
1. db_id 复用源（object_name 不变）
2. base_path 默认复用源（idx/_DB_META 在共享盘，零搬迁），API 参数可覆盖
3. data 集中到 master host 本地 data_path
4. 最终 data 文件按**源 host 数**约束（每源 host 的数据 → master host 上一个独立 writer）
5. 并发走 task scheduler 派发 internal task（绕开 thread_local 障碍）；master host 无同 host worker → API 参数 `local_workers`（默认值）拉起

**两个关键实现决策**：
6. 新增 `__merge_object` internal task（task args 带 target_data_path，绕开 db_registry 单映射限制）
7. 自动删源：master 发删除消息给各源 host worker；**必须所有 merge task 全部成功才统一删**（失败则全保留，可重试）

---

## 1. 存储模型（设计契约，`architecture.md §3.3`）

```python
db = open_db("/shared/project_a", data_path="/ssd/local")  # 共享 + 本地
```

| 路径 | 共享性 | 存什么 |
|------|--------|--------|
| `base_path` | **共享**（所有 Master/Worker 可访问） | `_DB_META`、`_FROZEN`、`_VARS`、`<writer_id>.idx` |
| `data_path` | **本地**（仅写它的进程） | `data_<wid>_<NNN>.dat`（压缩数据本体） |

代码印证：`.idx`/`_DB_META`/`_FROZEN` 全写 `base_path`（`database.cpp:475,558,573`）；`.dat` 写 `data_path_.empty() ? base_path_ : data_path_`（`data_writer.cpp:23,26`）。

**推论**：
- 索引天然全局可见（共享盘），merge **不需要搬索引**。
- 真正分散的是 `.dat`（各 host 本地盘），merge 的核心工作就是把它集中到 master host。
- 跨机读 `.dat` 走 DataServer TCP（`DATA_REQUEST/RESPONSE` msg=11/12），已有完整范式。

---

## 2. API 设计

### 2.1 签名

```python
fly.merge_db(
    path: str,                      # 源 db 的 base_path（共享存储，必须已 freeze）
    data_path: str = "",            # 产物 data_path（master host 本地）。默认 path + ".merged_data"
    base_path: str = "",            # 产物 base_path。默认="" 表示复用源 path（idx 不搬）
    local_workers: int = 4,         # master host 无同 host worker 时拉起的 local worker 数（并发度）
    delete_source: bool = True,     # merge 全部成功后是否自动删源各 host 的原 .dat
) -> '_Database'
```

- **master-only**（仿 `load_db`，只在 `Master` 类定义，`Worker` 调用自然 `AttributeError`）。
- 返回合并后的 `_Database` 句柄（已 register，可直接 read）。

### 2.2 前置条件与调用约束

- 源 db **必须已 freeze**（`path/_FROZEN` 存在）。freeze 保证全员 flush、idx 稳定、无并发写。
  - 未 freeze → `RuntimeError("merge_db requires frozen db; call db.freeze() first")`。
- 源 db 的所有 writer 对应的 host **当前有在线 worker**（merge 期间需要这些 worker 既作数据源、又可能作删除执行者）。
  - 某 host 无在线 worker → 仿 `load_db` 流程，spawn 一个 `--host` 匹配的 worker（见 §3.2 Phase 2）。

**调用约束**（保证 merge 期间数据分布稳定）：

1. **前置等待**：调用 `merge_db` 时若仍有 pending/running task（任何 db 的），会**先等待它们全部完成**（`wait_for_all_tasks`，1h 超时）再开始 merge。freeze 已禁止该 db 的写入，但其他 db 的 task 完成可能改变 master/worker 状态，必须等齐。merge 派发的 `__merge_object` task 在等待之后才提交，不会被本等待误拦。
2. **阻塞调用**：`merge_db` 是同步阻塞调用——在返回前完成全部工作（等待已有 task → 派发 merge task → 等待完成 → 删源 → 状态清理）。调用方（用户脚本）在 merge 完成前不会继续执行后续代码。master 主线程执行用户脚本，reactor 在独立线程处理消息，两者不冲突。

---

## 3. merge 完整流程（master 主导，5 阶段）

### Phase 1: 校验 + 前置等待 + 读源 meta

```
├─ 检查 path/_FROZEN 存在（freeze 前置）
├─ 若有 pending/running task：wait_for_all_tasks(1h)  # 限制 1：保证数据分布稳定
├─ 读 path/_DB_META：db_id + WorkerInfo 列表（writer_id, hostname, worker_id）
├─ 构造产物路径：
│    merge_base_path = base_path if base_path else path   # 默认复用源
│    merge_data_path = data_path if data_path else path + ".merged_data"
└─ 在 master host 创建 merge_data_path 目录（本地）
```

**db_id 复用源**（决策 1）：object_name `db_id:short` 不变，`__merge_object` task 可直接用源 db_id 拉源对象。

### Phase 2: 确保目标 worker 池就位

```
├─ 从 _DB_META 提取所有源 hostname 集合：source_hosts = {w.hostname for w in meta.workers}
├─ 查 master host 上现有同 host worker：
│    master_host_workers = [wid for wid, h in get_worker_hostnames() if h == master_hostname]
├─ 若 master_host_workers 为空：
│    按 local_workers 数 spawn local worker（_spawn_process_worker，不传 --host，自然同 master host）
│    wait_for_all_workers(timeout=30)
│    master_host_workers = 重新查询
└─ 确认所有 source_hosts 也有在线 worker（无则 spawn --host 匹配的，仿 load_db Phase 2）
```

**目标 worker 池** = master host 上的 worker（用于执行 `__merge_object` 落盘 + 后续删源）。
**源 worker 池** = 各 source_host 上的 worker（用于被跨机读 + 接收删除命令）。

### Phase 3: master 直读共享 base_path 的全部 idx（无网络）

```
├─ 对每个 WorkerInfo.writer_id：
│    LocalIndex idx(path + "/" + writer_id + ".idx"); idx.load()
│    entries = idx.get_all_entries()  # IndexEntry 列表（含 object_name, file_name, offset, size, host, hash）
└─ 按 source_host 分组：host_to_entries = {hostname: [IndexEntry, ...]}
   （IndexEntry.host_ 字段记录写入时 host，用于分组）
```

master 此时持有"每个源 host 上有哪些对象"的完整清单。

### Phase 4: 派发 `__merge_object` tasks（并发，按源 host 分配目标 worker）

**关键设计**（决策 4 + 6）：
- 最终 data 文件按源 host 数约束 → 每个源 host 的对象，固定派给 master host 上**同一个目标 worker**。
- 这样每个目标 writer 只写自己负责的那批对象，文件数 = 源 host 数（受 aggregation_threshold 切分）。
- 目标 worker 池轮转分配：`target_worker = master_host_workers[i % len(master_host_workers)]`。

```
target_assignments = {}  # hostname → target_worker_id
for i, hostname in enumerate(source_hosts):
    target_assignments[hostname] = master_host_workers[i % len(master_host_workers)]

pending_merge_tasks = []  # 跟踪所有派发的 task，用于 Phase 5 的"全部完成"判定
for hostname, entries in host_to_entries.items():
    target_worker = target_assignments[hostname]
    for entry in entries:
        short_name = strip_db_id_prefix(entry.object_name)  # "db_id:short" → "short"
        # 派发 __merge_object internal task
        task_id = remote_task_counter_.fetch_add(1)
        TaskAssignMessage:
            task_name_ = "__merge_object"
            task_module_ = "__fly_internal"
            args_ = {short_name, db_id, merge_data_path, source_host=hostname}
            write_context_hash_ = entry.write_context_hash_
        reactor_->send(target_worker_conn, assign)
        pending_merge_tasks.append((task_id, hostname, short_name))
```

**`__merge_object` task 执行体**（worker 端 `execute_internal_task` 分支，**方案 B：独立 DataWriter，不构造 Database**）：

```cpp
if (task.task_name_ == "__merge_object") {
    // args: [short_name, db_id, base_path, target_data_path, source_host]
    execute_merge_object(task.task_id_, short_name, db_id, base_path, target_data_path);
}

void execute_merge_object(task_id, short_name, db_id, base_path, target_data_path) {
    full = db_id + ":" + short_name;
    ds = DataService::instance();

    // 1. 跨机拉源压缩字节（本地必 miss，自动 TIER2/TIER3 回源）。
    auto [found, comp, ...] = ds->read_raw_compressed(full);

    // 2. 解析 ObjectHeader 拿 total_size / chunk_count。
    ObjectHeader header = ObjectHeader::deserialize(comp, ...);

    // 3. 独立 DataWriter 落盘（不构造 Database，避免 DataService 全局状态污染）。
    //    见 §5.1：Database 构造会 register/析构会 unregister，污染 db_paths_。
    DataWriter* writer = get_or_create_merge_writer(base_path, target_data_path);
    ds->register_database(db_id, base_path, target_data_path, writer->writer_id());
    ds->on_write_started(db_id, full);
    writer->write_record(full, header.total_size_, header.chunk_count_, *comp, source_hash);
    writer->flush();

    // 4. 登记 local_idx_（让本 worker DataServer 能服务 merge 后的对象）。
    //    只登记本次新写的 entry（get_last_entry），不登记从源 idx 加载的历史 entry。
    auto last = writer->get_last_entry(full);
    if (last) { ds->on_write_completed(db_id, full, {last.value()}); ds->on_object_flushed(full); }

    // 5. TaskComplete（不调 register_write_with_master——db 已 freeze 会被拒；
    //    master 的 on_task_complete internal 分支已调 update_remote_idx 登记对象位置）。
    TaskCompleteMessage complete; complete.task_id_ = task_id;
    complete.is_internal_ = true;
    complete.written_objects_.push_back({full, comp->size()});
    reactor_->send(master_conn_, complete);
}
```

**为何不用 `Database::backup_object`（方案 A）**：见 §5.1。构造 Database 会覆盖 `db_paths_[db_id]` 或触发 base_path 唯一性失败；析构会 `unregister_database` 污染全局；`do_backup_write` 需 Database 成员。方案 B 用独立 DataWriter 完全绕开。

**merge_writer 缓存**：worker 维护 `merge_writers_`（target_data_path → DataWriter），跨 task 复用同一 writer（设计 §5.3：每源 host 一个 writer），避免 per-object 构造造成文件爆炸。

**并发保证**：
- task scheduler 原生并发（每 worker 一个 task 槽，多 worker 并行）。
- master host 上多个 local worker → 多个 task 槽 → 多对象并行拉取 + 落盘。
- DataServer send_thread_pool 并行服务跨机读请求。
- 每个 target worker 各自的 merge_writer 互不干扰。

### Phase 5: 等待全部完成 + 统一删源（决策 7）

```
├─ 等待 pending_merge_tasks 全部 TaskComplete（带总超时，如 3600s）
│    逐个 ack 标记完成；任一 TaskFailed → 记录失败，继续等其他（不立即 abort）
│
├─ 判定：
│    if 全部成功 and delete_source:
│        进入删源流程（见下）
│    elif 有失败:
│        不删源（保留源 .dat），返回部分成功结果 + 缺失对象列表
│        用户可修正后重试（重试时已成功的对象会被再次拉取覆盖，幂等）
│
└─ 删源流程（仅全部成功时执行）：
     对每个 source_host：
       找该 host 上一个在线 worker（源 worker 池）
       发送 DeleteDataMessage(db_id, source_host 的 writer_ids, path)
         ← 新增消息类型（见 §4.1）
       worker 收到后：删除本地 data_path 下对应 writer_id 的 .dat 文件
       回 DeleteDataAck
     等待所有 DeleteDataAck
     在源 _DB_META 追加 merge 完成标记（merge 时间、产物路径、已删源 host 列表）
```

**"全部完成才统一删"的语义保证**：
- merge task 部分失败时，源 .dat 完整保留 → 用户重试 merge 仍能拉到全部源数据。
- 只有 100% 成功才删源 → 删源后产物是唯一副本，但此时已验证完整。
- 删源失败（某 host worker 离线）→ 该 host 的源保留，记入 _DB_META，不阻塞其他 host 删除。

---

## 4. 新增内容清单

### 4.1 新增消息类型（2 个）

| 消息 | 方向 | 字段 | 用途 |
|------|------|------|------|
| `DeleteDataMessage` | M→W | `db_id_`, `CMVector<CMString> writer_ids_`, `CMString base_path_` | 命令 worker 删除本地 data_path 下指定 writer 的 .dat |
| `DeleteDataAck` | W→M | `worker_id_`, `db_id_`, `bool success_`, `CMString error_message_`, `int32_t deleted_count_`, `CMVector<CMString> deleted_writer_ids_` | 删除结果回报 |

枚举槽位：在 `MessageType` 现有最大值后追加（当前 max=41，新增 42/43）。无需复用 `IDX_REQUEST/RESPONSE`（15/16）——那是 idx 传输的死代码，merge 不需要（索引在共享盘）。

### 4.2 新增 internal task

`__merge_object`（task_name，`task_module_="__fly_internal"`），在 `worker_agent.cpp:execute_internal_task` 加分支（仿 `__backup_object` line 1175）。

### 4.3 新增 C++ 方法

| 文件 | 方法 | 用途 |
|------|------|------|
| `master_agent.{h,cpp}` | `send_merge_task(target_worker, short_name, db_id, target_data_path, source_host)` | 派发单个 `__merge_object` task |
| `master_agent.{h,cpp}` | `send_delete_data(worker_id, db_id, writer_ids, base_path)` | 派发删除命令 |
| `master_agent.{h,cpp}` | `wait_merge_tasks_complete(task_ids, timeout)` | 等待一批 task 全部完成（复用现有 pending task 机制） |

### 4.4 新增 Python API

| 文件 | 改动 |
|------|------|
| `src/fly/__init__.py` | 定义 `merge_db(...)` 委托 `get_agent().merge_db(...)`；加 `__all__`（仿 `load_db` line 71-82） |
| `src/agent/py/agent.py` | `Master` 类加 `merge_db` 方法，实现 §3 的 5 阶段编排。**不动 `Worker` 类** |
| `src/agent/export/agent_export.cpp` | `FLY_EXPORT_METHOD` 暴露 §4.3 的 C++ 方法（仿现有 `send_idx_load_to_worker`） |

### 4.5 不需要触碰

- **索引传输消息**：不需要。idx 在共享 base_path，master 直读。
- **MessageProtocol / Reactor / DataServer**：泛型，自动适配新消息。
- **fly.sh install / main.cpp**：不新增模块。
- **DataWriter / LocalIndex**：merge 落盘复用现有路径，不改。

---

## 5. 关键设计权衡记录

### 5.1 为何不直接用 `__backup_object`（而新增 `__merge_object` + 方案 B 独立 DataWriter）

`__backup_object`（`worker_agent.cpp:1175`）通过 `request_db_path(db_id)` 拿 db 路径——但 `db_registry_` 是 `db_id → {base_path, data_path}` **全局单映射**（`master_agent.cpp:1124`），无法让不同 worker 对同 db_id 用不同 data_path。

merge 需要目标 worker 落盘到 **master host 本地 data_path**（不同于源 data_path）。实现上评估了两个方案，最终选 **方案 B（独立 DataWriter，不构造 Database）**：

| 方案 | 问题 | 结论 |
|------|------|------|
| A：构造临时 `Database`（base_path=源, data_path=target, db_id=源） | 构造时 `Database::Database` 调 `DataService::register_database`（`database.cpp:30`），覆盖 `db_paths_[db_id]` 或触发 base_path 唯一性失败（`data_service.cpp:104-108`）；析构调 `unregister_database`（`database.cpp:68`）污染全局；`do_backup_write` 还依赖 Database 成员 | ❌ 全局状态污染 |
| **B：独立 `DataWriter` + 手动 DataService 登记** | DataWriter 不依赖 DataService/Database 全局状态（`data_writer.cpp` 自包含）；手动 `register_database` + `on_write_completed` 登记 local_idx_；不调 `register_write_with_master`（db 已 freeze 会被拒，改由 master `on_task_complete` internal 分支 `update_remote_idx`） | ✅ 已采用 |

### 5.2 为何并发走 task scheduler 而非 master 进程内 ThreadPool

`WorkerAgentContext` 全部是 `thread_local`（`worker_context.h:173-184`，`register_func_`/`record_write_func_` 等）。master 进程内开 ThreadPool 并发调 `do_backup_write` 时，worker 线程拿不到 master 注入的 callback → 失败。

走 task scheduler 派发到独立 worker 进程，每个 worker 进程有自己的 `WorkerAgentContext`（main 线程注入 callback），天然支持并发。多个 master host local worker = 多进程并发 = 多 task 槽并行。

### 5.3 为何 data 文件按源 host 数约束（而非合并成单文件）

- **避免单巨型 .dat**：大 db 合并成单文件，后续读放大、删困难。
- **按源 host 分组落盘**：每源 host 的数据 → master host 上一个独立 writer（一组 `data_<wid>_*.dat`），受 `aggregation_threshold` 自然切分。文件数 = 源 host 数 × 切分段数，可控。
- **语义清晰**：merge 后能追溯"这批数据来自哪个源 host"。

### 5.4 "全部完成才统一删源"的语义

- **强一致性**：merge task 部分失败时源完整保留 → 重试幂等（已成功对象会被覆盖）。
- **删源原子性**：只在 100% 成功后触发删源；删源本身尽力而为（某 host 失败不阻塞其他），失败项记入 _DB_META。
- **代价**：大 db merge 期间双份数据（源 + 产物），需 master host 有足够磁盘。

### 5.5 删源实现细节：删 data_dir 全部 .dat（而非按 writer_id 前缀）

实现中发现：idx 的 `file_name_` 字段（指向 .dat）在跨进程写入场景下**可能与磁盘 .dat 文件名不一致**——master 进程和 worker 进程各自 `open_db` 创建 Database（不同 writer_id），写到同一共享 base_path，导致 idx entry 的 file_name（某进程 writer_id）与实际 .dat（另一进程 writer_id）错配。

因此 `on_delete_data` 的实现不依赖 idx 解析 file_name，而是**删除源 data_dir 下全部 `data_*.dat`**。安全性保证：data_dir 是该 db 的 data_path（一个 db 一个 data_dir），所有 .dat 都属于该 db，merge 全量迁移后全删是正确的。

### 5.6 merge 后状态清理：广播 MergeCleanupMessage + worker 自主重建 local_idx

**问题**：merge 改变数据分布后，各进程残留的旧索引会导致读路径失效：
- master / 各 worker 的 `local_idx_[db_id]` 仍指向已删源 .dat → TIER1 读必失败 + `Data file not found` ERR 日志
- master / 各 worker 的 `remote_idx_[db_id]` 缓存了失效的源 worker 位置 → TIER2 远程读试源 worker（源下线时卡 30s 超时）

**方案**：merge 全部成功 + 删源完成后，master 广播 `MergeCleanupMessage(db_id, base_path, data_path, exempt_worker_ids)` 给**所有 worker**，并清理自身状态：

master 侧（`cleanup_after_merge`）：
1. 广播 MergeCleanup（exempt = merge target workers，它们的新 local_idx 有效，跳过）
2. 清自身 `local_idx_[db_id]` + `remote_idx_[db_id]`（`DataService::clear_local_index_for_db` / `clear_remote_index_for_db`，不清 ObjectCache——数据内容未变，cache 仍是正确副本）
3. 重建 `remote_idx_`：对每个 merge 对象登记 merge target worker 位置（`update_remote_idx`），让调度/读路径找到数据
4. 更新 `db_registry_[db_id]` 指向 merge 路径

worker 侧（`on_merge_cleanup`）：
1. 若本 worker 在 exempt 列表（merge target）→ 跳过（保留有效 local_idx）
2. 否则清旧 `local_idx_[db_id]` + `remote_idx_[db_id]`
3. `register_database(db_id, base_path, data_path, ...)` 更新 db_paths_
4. **遍历共享 base_path 下所有 `.idx`，load 新 entry 到 local_idx**（新 idx 由 merge worker 写在共享盘）
   - 若 data_path 可达（同机本地盘或共享 FS）→ 后续读可**本地直读 .dat**，不走远程读
   - 若不可达 → local_idx 有 entry 但 .dat 读不到，TIER1 失败后回退 TIER2/TIER3（仍正确）

**为什么让 worker load 新 idx 而非只清理**：merge 后 idx 在共享 base_path（master 也可达），让能访问 data_path 的 worker 重建 local_idx 后，本地读成立——减少跨机读，这正是 merge "数据集中"的核心价值。只清理而不重建会让所有 worker 都依赖远程读，浪费了集中化的收益。

**不清 ObjectCache 的理由**：merge 是数据**位置迁移**，对象**内容未变**。ObjectCache 存的是压缩字节副本（位置无关），命中即返回正确数据。清了反而逼重拉，浪费。

### 5.7 merge_db 读 meta 不构造 Database（静态 load_meta）

`merge_db` 在已 `open_db` 的 master 进程内调用时，若构造 `_Database(path)` 读 meta 会生成新 db_id（`generate_db_id` 含随机后缀），与 open_db 的 db_id 不同但 base_path 相同 → 触发 `DataService::register_database` 的 base_path 唯一性 ERROR。

修复：新增 `Database::load_meta_from_path(path)` 静态方法（不构造实例、不 register），导出为 `ex_stg_load_meta_from_path`，Python `_Database.load_meta_from_path` 包装。merge_db 用它读 meta。

### 5.8 产物 db 句柄用源 db_id 构造（with_id）

merge_db 返回的产物句柄用 `ex_stg_create_database_with_id(base, data, 0, db_id)` 构造（源 db_id），避免生成新 db_id 触发 base_path 重复注册。object_name 保持 `源db_id:short` 一致，read_object 走 master remote_idx（merge task 已登记对象位置到 merge worker）。

---

## 6. 实现步骤建议（TDD）

| 步骤 | 内容 | 验证 |
|------|------|------|
| 1 | 新增 `DeleteDataMessage`/`DeleteDataAck` 消息 + round-trip 测试 | `message_protocol_test.cpp` |
| 2 | worker 端 `__merge_object` task 分支（构造临时 Database + `backup_object`） | `worker_agent_test.cpp` 单测：mock 源对象 → merge 到 target_data_path |
| 3 | master 端 `send_merge_task` / `send_delete_data` / `wait_merge_tasks_complete` | `master_agent_test.cpp` 单测 |
| 4 | Python `Master.merge_db` 5 阶段编排 | QA 两阶段 run（见下） |
| 5 | `fly.merge_db` 导出 + `__all__` | `test_executor.py` 风格 |
| 6 | QA: 多 host 写本地 data_path → freeze → merge_db → 校验产物自包含 + 源已删 | `qa/storage/test_merge_db.py` |

---

## 7. 开放问题（少量，不阻塞实现）

### Q1. `merge_data_path` 的默认值

默认 `path + ".merged_data"`。但若 base_path 复用源（默认），则 data_path 也在源 base_path 同目录下——可能与其他 db 的 data_path 冲突？需确认 master host 本地路径唯一性。**倾向**：默认用 `path + ".merged_data"`，用户也可显式传。

### Q2. merge 期间源 db 的读并发

merge 时源 worker 仍可能被其他任务读。`__merge_object` 的 `read_raw_compressed` 是只读拉取，不影响源。删源阶段才需保证无并发读（freeze 已保证无新写，但读仍在）。**倾向**：删源前再次确认无活跃读（或接受读失败，因为 freeze 后 read-only db 通常无持续读）。

### Q3. 产物 freeze 状态

merge 完成后产物是否自动 freeze？**倾向**：是。产物是不可变快照，freeze 后语义清晰，且防止误写。在 Phase 5 末尾调 `target_db.freeze()`。

### Q4. IDX_REQUEST/RESPONSE 死枚举（15/16）处置

确认冗余（索引在共享盘）。**倾向**：保留槽位 + 加注释 `// reserved, unused — idx is on shared base_path, see db-merge-design.md`。

---

## 8. v1 → v2 → v3 → v4 演进

| 维度 | v1 | v2 | v3 | v4（定稿） |
|------|----|----|----|-----------|
| 前提 | 共享 FS | data 本地 | base_path 共享契约 | 同 v3 |
| 触发 | freeze 自动 | freeze/离线 | 主动 API | **主动 API** |
| 搬运对象 | idx 合并 | idx+data | .dat（复用 backup） | **.dat 集中到 master host** |
| 并发 | 未明 | 网络 idx | 未明 | **task scheduler + local workers** |
| 文件组织 | merged.idx | merged.idx | 自包含单目录 | **按源 host 数约束** |
| 删源 | 不涉及 | 不涉及 | 不涉及 | **全部成功后统一删** |
| 新消息 | 无 | IdxReq/Resp | 无 | **DeleteData + Ack** |
| base_path | 新建 | 新建 | 新建 | **默认复用源** |

---

*文档制定日期：2026-07-22（v4 定稿）*
*基于 `architecture.md §3.3` 双路径契约 + commit `e1aac14` 源码核实 + 用户 7 项决策*
