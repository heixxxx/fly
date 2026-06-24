# Issue #001: 失败 Task 重跑导致的写入数据重复及 load_db 读取错误

**状态**: Resolved（2026-06-25）— 通过事务化段标记 + 异常清理解决
**严重程度**: High — 数据正确性
**影响模块**: `storage`（LocalIndex, DataService, DataWriter, WriteBackQueue）
**创建日期**: 2026-05-24

---

## 1. 问题描述

当 `restart_failed_tasks()` 重新执行一个**曾经成功写入过数据**的 task 时，Worker 使用同一个 writer_id（同一 Database 实例）再次写入同一 object_name，导致：

1. **idx 文件产生重复条目**：同一 object_name 在 `{writer_id}.idx` 中出现多条 IndexEntry
2. **data 文件中存在冗余数据**：同一对象值被写入 data 文件两次
3. **读取时返回错误数据**：
   - 本地读取（`find_entry`）返回**旧数据**（第一次写入的值）
   - load_db 场景（`restore_entries` + `read_from_entries`）返回**数据拼接**（两次写入的值拼接在一起）

---

## 2. 影响场景

### 场景 A：Task 成功写入后因后续步骤失败 → restart_failed_tasks 重跑

```
Task-1: db.write_object("key_x", 100)     # 成功写入
Task-2: 依赖 key_x，但因其他原因失败       # Task-2 FAILED
→ restart_failed_tasks()
→ Task-1 重跑: db.write_object("key_x", 200)  # 第二次写入
→ Task-2 重跑: db.read_object("key_x")        # 读到什么？
```

**预期**: 读到 200（最新写入）
**实际**: 取决于读取路径，可能读到 100（旧数据）或拼接数据。

### 场景 B：Task 写入后 Worker crash → 新 Worker 执行相同 task

```
Worker-A: Task-1 写入 key_x=100
Worker-A: crash（idx 已持久化，data 已落盘）
Master:   恢复 Task-1，分配给 Worker-B
Worker-B: Task-1 重跑，写入 key_x=200（使用 Worker-B 自己的 writer_id）
```

此场景中 Worker-B 使用不同 writer_id，idx 和 data 文件不同，**同进程内写入不会产生重复**。但 Worker-A 的旧数据仍占用磁盘空间。

**⚠️ 但 load_db 场景下此场景会触发数据损坏**，见下方场景 E。

### 场景 C：load_db 后新 Worker 加载含重复条目的 idx（同一 writer_id）

```
原始运行：Worker-A（writer_id=aaa）写 key_x 两次（场景 A）
DB 目录：
  ├── aaa.idx           # key_x 有两条 IndexEntry（offset_1, offset_2）
  ├── data_aaa_000.dat  # key_x 两份数据（100 和 200）
  
load_db("/path"):
  新 Worker-C 收到 IdxLoadCommand{writer_ids: [aaa]}
  → 加载 aaa.idx → restore_entries() 纯追加 → local_idx_["key_x"].entries 有两条
  → 新 Worker-C 执行 task 读取 key_x:
    → read_from_entries([entry_1, entry_2])
    → 当作 large object block 拼接 → 返回 100+200 的拼接数据 → 数据损坏
```

### 场景 E：load_db 加载跨 writer_id 的重复数据（最常见）

> 此场景是场景 B 的 load_db 后续，是最可能在实际生产中触发的路径。

```
初始运行：
  Worker-A（writer_id=aaa）: Task-1 写入 key_x=100
    → aaa.idx 有 key_x entry（offset → data_aaa_000.dat）
    → data_aaa_000.dat 有 key_x=100 的数据
  Worker-A crash
  Master 恢复 Task-1，分配给 Worker-B
  
  Worker-B（writer_id=bbb）: Task-1 重跑，写入 key_x=200
    → bbb.idx 有 key_x entry（offset → data_bbb_000.dat）
    → data_bbb_000.dat 有 key_x=200 的数据

DB 目录：
  ├── aaa.idx           # key_x → data_aaa_000.dat offset=X size=Y
  ├── data_aaa_000.dat  # key_x=100
  ├── bbb.idx           # key_x → data_bbb_000.dat offset=X size=Y
  ├── data_bbb_000.dat  # key_x=200

load_db("/path"):
  Master 广播 IdxLoadCommand{writer_ids: [aaa, bbb]} 给新 Worker-C
  Worker-C:
    → 加载 aaa.idx → restore_entries → local_idx_["key_x"].entries = [entry_aaa]
    → 加载 bbb.idx → restore_entries → local_idx_["key_x"].entries = [entry_aaa, entry_bbb]  // 纯追加！
    → 读取 key_x:
      → read_from_entries([entry_aaa, entry_bbb])
      → entry_aaa → find_file_path("data_aaa_000.dat") → 读到 100
      → entry_bbb → find_file_path("data_bbb_000.dat") → 读到 200
      → 拼接 → 返回 100+200 的字节拼接 → 数据损坏！
```

**关键区别**：
- 场景 C：重复来自**同一 writer_id** 的 idx（同文件内多条 entry）
- 场景 E：重复来自**不同 writer_id** 的 idx（不同文件各一条 entry，指向不同 data 文件）

**场景 E 更常见的原因**：
- Worker crash 后 Master 恢复任务，天然会分配给不同 Worker（不同 writer_id）
- `on_disconnect` 恢复 RUNNING 任务也会产生跨 writer_id 重复
- 不需要 task 失败重跑，只要 Worker 断连就可能触发

### 场景 D：Task 写入中间态（已注册但未落盘）→ 重跑

```
Worker-A: Task-1 调用 write_object("key_x", 100)
  → on_write_started → local_idx_["key_x"] = INCOMPLETE
  → DataWriter 写入 data 文件（完成）
  → on_write_completed → local_idx_["key_x"] = COMPLETE, entries=[entry_1]
  → 向 Master 注册 write_register("key_x")
  → Task-1 因后续异常 FAILED（非 write_object 异常）
  
restart_failed_tasks():
  Task-1 重跑: write_object("key_x", 200)
  → on_write_started → local_idx_["key_x"] 被覆盖为 INCOMPLETE（新 info）
  → on_write_completed → local_idx_["key_x"].entries = [entry_2]
  → Master 收到重复的 write_register("key_x")
  → Master 的 on_write_register 正常处理（幂等，更新 remote_idx）
```

**场景 D 结论**：由于 `on_write_started` 会**覆盖** `local_idx_[object_name]`（非追加），`on_write_completed` 设置 `entries = entries`（赋值，非追加），同一 Worker 进程内的重跑**不会产生 entries 重复**。问题仅出现在 idx 文件加载路径（`restore_entries`）和直接 LocalIndex 查询（`find_entry`）。

---

## 3. 根因分析

### 根因 1：`LocalIndex::find_entry()` 返回 `front()`

**文件**: `src/storage/cpp/local_index.cpp:69-75`

```cpp
IndexEntry* LocalIndex::find_entry(const CMString& object_name) {
    auto it = entries_.find(object_name);
    if (it == entries_.end() || it->second.empty()) {
        return nullptr;
    }
    return &(it->second.front());  // ← 返回第一条（最旧）
}
```

当同一 object_name 在 idx 文件中有多次写入时（`entries_["key_x"] = [entry_v1, entry_v2]`），`find_entry` 返回第一次写入的 entry，指向旧数据。

**影响范围**：直接通过 LocalIndex 查询的读取路径（非 DataService 路径）。

### 根因 2：`DataService::restore_entries()` 纯追加不去重

**文件**: `src/storage/cpp/data_service.cpp:432-458`

```cpp
void DataService::restore_entries(const CMString& db_id,
                                    const CMVector<IndexEntry>& entries) {
    CMUnorderedMap<CMString, CMVector<IndexEntry>> grouped;
    for (const auto& e : entries) {
        grouped[e.object_name].push_back(e);
    }
    // ...
    for (auto& [object_name, obj_entries] : grouped) {
        // Append entries (don't replace — there might be existing entries)
        for (auto& e : obj_entries) {
            info->entries.push_back(std::move(e));  // ← 纯追加，不去重
        }
    }
}
```

load_db 时 `restore_entries` 加载 idx 中的所有条目并**追加**到 `local_idx_`。如果 `aaa.idx` 中 `key_x` 有两条 entry，两条都会被追加。

### 根因 3：`DataReader::read_from_entries()` 拼接多条 entry

**文件**: `src/storage/cpp/data_reader.cpp:182-218`

```cpp
ReadResult DataReader::read_from_entries(const CMVector<IndexEntry>& entries) {
    if (entries.size() == 1) {
        return read_object_data(entries.front());  // ← 单条正常
    }
    // 多条 → 排序 → 逐条读取 → 拼接
    for (size_t i = 0; i < sorted.size(); ++i) {
        // 读取 + decompress
        result.insert(result.end(), decompressed.begin(), decompressed.end());
    }
}
```

`read_from_entries` 的语义是**将多条 entry 视为 large object 的不同 block** 并拼接。当同一 object_name 有重复 entry 时（非 large object block），会将两份完整数据拼在一起 → 返回损坏数据。

---

## 4. 影响总结

| 场景 | find_entry | restore_entries | read_from_entries | 实际读到 |
|------|-----------|----------------|-------------------|----------|
| A: 同 Worker 重跑 | 不走此路径* | 不走此路径* | 不走此路径* | 正确（on_write_started 覆盖） |
| B: 不同 Worker 重跑 | 不走此路径* | 不走此路径* | 不走此路径* | 正确（不同 writer_id，独立 entries） |
| C: load_db（同 writer_id 重复） | N/A | 追加两条（同文件） | 拼接 v1+v2 | **损坏** |
| D: 中间态重跑 | 不走此路径* | 不走此路径* | 不走此路径* | 正确（on_write_started 覆盖） |
| **E: load_db（跨 writer_id 重复）** | N/A | **追加两条（不同文件）** | **拼接 v1+v2** | **损坏** |
| 直接查 LocalIndex | 返回 v1（旧） | N/A | N/A | **旧数据** |

\* 同进程内写入走 `DataService::on_write_started` → 覆盖 `local_idx_`，`on_write_completed` 赋值 `entries`，不产生重复。

**结论**：所有**运行时**写入路径均正确（on_write_started 覆盖机制）。问题仅出现在 **load_db → restore_entries → read_from_entries** 路径，以及直接通过 **LocalIndex::find_entry** 查询时。场景 E（跨 writer_id）是最常见的触发路径。

---

## 5. 修复方案

### 方案 A：`find_entry()` 返回 `back()`（推荐）

```cpp
// local_index.cpp:69-75
IndexEntry* LocalIndex::find_entry(const CMString& object_name) {
    auto it = entries_.find(object_name);
    if (it == entries_.end() || it->second.empty()) {
        return nullptr;
    }
    return &(it->second.back());  // ← 改为返回最后一条（最新）
}
```

**优点**：
- 语义正确：同一对象多次写入，最后一次为最新值
- 改动最小：仅改一行
- `find_all_entries()` 不受影响（large object 仍读全部 block 后排序拼接）

**影响范围**：所有通过 LocalIndex 直接查询的读取路径。

### 方案 B：`restore_entries()` 去重（推荐，与方案 A 配合）

```cpp
// data_service.cpp:432-458
void DataService::restore_entries(const CMString& db_id,
                                    const CMVector<IndexEntry>& entries) {
    CMUnorderedMap<CMString, CMVector<IndexEntry>> grouped;
    for (const auto& e : entries) {
        grouped[e.object_name].push_back(e);
    }

    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [object_name, obj_entries] : grouped) {
        auto& info = local_idx_[object_name];
        if (!info) {
            info = CMMakeShared<LocalObjectInfo>();
        }
        info->db_id = db_id;
        // 去重：同一 object_name 只保留最后一条 entry（最新写入）
        if (!obj_entries.empty()) {
            info->entries = {obj_entries.back()};
        }
        info->completion_state = CompletionState::COMPLETE;
        info->flushed = true;
    }
}
```

**优点**：
- 解决 load_db 场景下的数据拼接问题
- 与方案 A 独立但互补：A 解决 LocalIndex 直接查询，B 解决 restore 加载

**注意**：此修改改变了 `restore_entries` 的语义——从"追加"变为"替换"。需确认当前所有调用方均期望此行为。

### 方案 C：写入前检查（可选补充，非必须）

在 `write_object` 写入前检查该 object_name 是否已有 entry：
- 若有，且不在 frozen 状态，可先发出 WARN 日志
- 不阻止写入（因为重跑是合法场景），仅提供诊断信息

**评级**：Low priority，仅作为可观测性增强。

---

## 6. 修复后的场景验证

### 场景 A（同 Worker 重跑）

```
修复前: find_entry → front() → v1（旧）
修复后: 不走 find_entry（走 on_write_started 覆盖路径）→ v2（正确）
```

### 场景 C（load_db，同 writer_id 重复）

```
修复前: restore_entries 追加 → entries=[v1, v2]（来自同一 idx 文件）→ read_from_entries 拼接 → 损坏
修复后（方案 B）: restore_entries 去重 → entries=[v2] → read_from_entries 单条 → v2（正确）
修复后（无方案 B，仅方案 A）: find_entry → back() → v2（如果走 LocalIndex 路径则正确）
```

### 场景 E（load_db，跨 writer_id 重复）

```
修复前: restore_entries 追加 → entries=[entry_aaa(v1), entry_bbb(v2)]（来自不同 idx 文件）
  → read_from_entries 拼接 → v1+v2 字节拼接 → 损坏
修复后（方案 B 改进版）:
  → 检测到 entry_aaa 和 entry_bbb 的 file_name 不同（data_aaa vs data_bbb）
  → 但 object_name 相同 → 不是 large object（large object 的 block 在同一 file_name 内）
  → 只保留最后一条（entry_bbb）→ entries=[entry_bbb] → v2（正确）
```

### large object 正确性

Large object 同一 object_name 的多条 entry 是不同 block（不同 offset），不是重复写入。

- 方案 A：`find_entry → back()` 只用于非 large object 场景，large object 走 `find_all_entries`，不受影响
- 方案 B：`restore_entries` 去重只保留一条 entry → **会破坏 large object**！

**⚠️ 方案 B 风险**：如果同一 object_name 的多条 entry 确实是 large object 的不同 block（而非重复写入），方案 B 会丢失 block。

**改进方案 B**：需要区分 "重复写入" 和 "large object block"。

**区分规则**：

| 条件 | 含义 | 处理 |
|------|------|------|
| 同一 object_name + 同一 file_name + 不同 offset | large object block | 保留全部 |
| 同一 object_name + 同一 file_name + 相同 offset | 同 worker 重复写入 | 只保留最后一条 |
| 同一 object_name + 不同 file_name | 跨 worker 重复写入（场景 E） | 只保留最后一条 |

注意：当前框架中 large object 的 block 写入到**同一个** data 文件（同一个 writer_id 的 DataWriter），所以跨 file_name 的多条 entry 一定是重复写入，不是 large object block。

```cpp
// 改进的去重逻辑
if (obj_entries.size() > 1) {
    // 检查是否为 large object（同一 file_name + 不同 offset → block 分片）
    auto& first = obj_entries.front();
    bool is_large_object = true;
    for (auto& e : obj_entries) {
        if (e.file_name != first.file_name) {
            // 不同 file_name → 一定是跨 worker 重复写入，不是 large object
            is_large_object = false;
            break;
        }
        if (e.offset == first.offset && &e != &first) {
            // 同 file_name + 同 offset → 重复写入，不是 large object
            is_large_object = false;
            break;
        }
    }
    
    if (is_large_object) {
        // large object block（同 file_name，不同 offset）→ 保留全部
        info->entries = std::move(obj_entries);
    } else {
        // 重复写入（同/跨 file_name）→ 只保留最后一条（最新）
        info->entries = {obj_entries.back()};
    }
} else {
    info->entries = std::move(obj_entries);
}
```

---

## 7. 建议实施步骤

1. **方案 A**：`find_entry()` → `back()` — 1 行改动，低风险
2. **方案 B（改进版）**：`restore_entries()` 去重 — 约 20 行改动，需覆盖单元测试
3. **补充测试**：
   - 失败 task 重跑写入同一 key 的 QA 测试
   - load_db 后读取重复写入 key 的 QA 测试
   - Large object + 重复写入混合场景的单元测试
4. **清理**（可选）：compaction 功能清理 idx/data 中的冗余数据

---

## 8. 关联文件

| 文件 | 相关代码 |
|------|----------|
| `src/storage/cpp/local_index.cpp:69-75` | `find_entry()` — 返回 `front()` |
| `src/storage/cpp/data_service.cpp:432-458` | `restore_entries()` — 纯追加 |
| `src/storage/cpp/data_service.cpp:79-87` | `on_write_started()` — 覆盖 local_idx_（正确行为） |
| `src/storage/cpp/data_service.cpp:89-105` | `on_write_completed()` — 赋值 entries（正确行为） |
| `src/storage/cpp/data_reader.cpp:182-218` | `read_from_entries()` — 多条 entry 拼接 |
| `src/storage/cpp/data_writer.cpp` | DataWriter — 追加写入 data 文件 |
| `src/agent/cpp/master_agent.cpp` | `restart_failed_tasks()` — 重跑入口 |

---

## 9. 最终解决方案（2026-06-25 实施）

上述方案 A/B 是针对"重复写入后读取损坏"的补救。最终采用了**根因方案**——事务化段标记 + 异常清理，从源头消除失败 task 的脏数据。

### 核心设计：idx op log 事务化段标记

在 idx op log 引入三个段边界标记（不含 task_id，纯段边界）：

| 标记 | 语义 | idx 行为 | data 文件行为 |
|------|------|---------|--------------|
| `BEGIN` | task 写入段开始 | 后续 ADD 进 pending 区 | 记录当前 data 偏移为回滚点 |
| `END` | 段成功提交 | pending 提交进 entries_ | 保留 |
| `ABORT` | 段异常撤销 | pending 丢弃 | data 文件 truncate 回 BEGIN 偏移 |

两种写入模式：
- **显式事务**（worker task 写入）：BEGIN 包裹的 ADD 进 pending，END 提交 / ABORT 回滚
- **隐式事务**（master 直接 write_object，无 task 状态）：ADD 不在任何段内，立即生效

`load()` 的 pending 区状态机：BEGIN → ADD 进 pending；END → pending 提交；ABORT → pending 丢弃；EOF 时 pending 非空 → 丢弃（崩溃遗留）。

### 异常清理（非崩溃）

worker 检测到 task 失败 → `Database::abort_task_writes()`：
1. `WriteBackQueue::clear_pending()` 清空未落盘的脏写（比 drain 高效）
2. `DataWriter::abort_segment()`：idx 打 ABORT + data 文件 truncate 回回滚点
3. 清运行时内存（DataService.local_idx_ / ObjectCache）

worker 向 master 发 `TaskFailedMessage`（新增 `dirty_objects_` 字段），master 清理 remote_idx/provenance/依赖图 + 广播 OBJECT_REMOVED。

### 崩溃恢复

崩溃 → 进程死亡 → idx 留下未闭合段（BEGIN 无 END/ABORT）→ load_db 时 pending 区自动丢弃脏 ADD（`had_unclosed_segment()` 诊断告警）。

### 连带修复

1. `on_task_failed` 增加持久化 failed task（之前只有调度失败才持久化，运行时失败的 task 不写 failed_tasks.bin，restart 无法恢复）
2. `schedule_tasks` 依赖不可解检测移到 `fail_unscheduleable_tasks` 开关之前（之前上游失败导致数据清理后，下游 pending task 要等 40s 才被判失败）

### 测试

- C++ 单测：LocalIndex 段标记（8 个）、DataWriter abort truncate（4 个）、WriteBackQueue clear_pending（2 个）
- QA：`test_task_failure_cleanup`（异常清理端到端）、`test_load_db_abort`（崩溃遗留段 load_db 丢弃）、`test_chain_failure_restart`（21 节点 DAG 连锁失败 + 跨进程 restart）
