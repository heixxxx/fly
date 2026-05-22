# Handoff: DB元数据持久化与load_db恢复方案设计

## 1. 当前状态

**项目**: Fly分布式任务框架 (C++20 + Python + nanobind)
**分支**: main, 最新commit: `fdbbd26`
**任务**: 设计并实现 `_DB_META` 动态增量更新 + `load_db` 恢复接口

### 已完成的前置工作
- Log模块fmt格式化 + CM_FORMAT_CLASS/CM_FORMAT_ENUM宏 (commit: `eadcab2`)
- LocalIndex增量持久化 (head+body格式) (commit: `6dddd66`)
- FailedTaskRecord增量持久化 (commit: `a90e74a`)
- 测试端口冲突修复 — port=0模式 (commit: `a90e74a`)
- CLAUDE.md文档更新 (commit: `fdbbd26`)
- 本轮对DB元数据、idx文件、DataService的完整代码调查

---

## 2. 问题描述

当前 `open_db` 创建的Database，进程退出后**无法恢复**：

| 组件 | 持久化？ | 恢复？ |
|------|---------|--------|
| 数据文件 (`.dat`) | ✅ 磁盘 | ✅ DataReader可读 |
| 索引文件 (`.idx`) | ✅ 磁盘增量 | ✅ LocalIndex.load()可恢复entries |
| DataService::local_idx_ | ❌ 纯内存 | ❌ 新进程为空 |
| DataService::remote_idx_ | ❌ 纯内存 | ❌ 新进程为空 |
| DataService::db_paths_ | ❌ 纯内存 | ❌ 新进程为空 |
| _DB_META | ⚠️ 只在freeze写,workers字段为空 | ❌ workers为空 |

**结果**: 新进程`open_db`同一路径 → db_id相同 → 可创建Database → 但DataService内存索引为空 → `try_read_local()` 和 `try_read_local_or_wait()` 全部返回false → 读不到任何已有数据。

---

## 3. 关键代码位置

### 3.1 数据写入流程 (已验证时序正确)

```
write_object() 模板 (database.h:48-73):
  1. DataService.on_write_started()         → local_idx_ 标记 INCOMPLETE
  2. WorkerAgentContext.register_write()      → 写前注册至Master
  3. WriteBackQueue.enqueue({execute, complete})
     execute: w->write_typed_object() + w->flush()
       flush: file_stream_.flush()  ← 数据先刷盘
              index_->save()        ← idx后刷盘
     complete: DataService.on_write_completed()  → local_idx_ 标记 COMPLETE
               DataService.on_object_flushed()   → local_idx_ 标记 flushed
               WorkerAgentContext.record_write() → DataReadyMessage
```

**关键**: idx文件中的记录一定对应已完全落盘的数据。load_db恢复时可安全标记COMPLETE+flushed。

### 3.2 _DB_META 现状

```cpp
// database.cpp:259-276 — create_frozen_marker()
void Database::create_frozen_marker() {
    // 写 _FROZEN 空文件
    // 写 _DB_META: DbMeta{db_id, base_path, frozen_at, frozen_at, {}}  ← workers为空!
}

// db_meta.h — 当前结构
struct WorkerInfo {
    uint64_t worker_id = 0;
    CMString host;
    CMString role;
    CMString data_path;
    CMString idx_file;
    int64_t idx_entry_count = 0;
    CMString launch_command;    // ← 已定义但从未使用
    FLY_SERIALIZE(worker_id, host, role, data_path, idx_file, idx_entry_count, launch_command)
};

struct DbMeta {
    CMString db_id;
    CMString base_path;
    int64_t created_at = 0;
    int64_t frozen_at = 0;
    CMVector<WorkerInfo> workers;  // ← 始终为空
    FLY_SERIALIZE(db_id, base_path, created_at, frozen_at, workers)
};
```

### 3.3 idx文件命名

```cpp
// data_writer.cpp:42-43
CMString idx_path = base_path_ + "/worker_" + std::to_string(worker_id_) + ".idx";
// data_reader.cpp:18
CMString idx_path = base_path_ + "/worker_" + std::to_string(worker_id_) + ".idx";
```

**命名规则**: `{base_path}/worker_{worker_id}.idx`

### 3.4 索引加载 (DataWriter/DataReader构造时)

```cpp
// data_writer.cpp:42-47
index_ = CMMakeUnique<LocalIndex>(idx_path);
if (fs::exists(idx_path)) {
    index_->load();  // 加载已有idx
}
// data_reader.cpp:20-23 — 同理
```

### 3.5 DataService内存索引 (新进程为空)

```cpp
// data_service.h — 核心数据结构
CMUnorderedMap<CMString, CMSharedPtr<LocalObjectInfo>> local_idx_;  // object_name → 写入状态
CMUnorderedMap<CMString, RemoteObjectInfo> remote_idx_;              // object_name → 远程位置
CMUnorderedMap<CMString, DbPaths> db_paths_;                         // db_id → 路径信息
```

### 3.6 Master的DB注册流程

```cpp
// master_agent.cpp:544-551
MasterAgent::get_or_create_database(base_path, data_path, writer_id) {
    auto db = CMMakeShared<Database>(base_path, data_path, writer_id);
    db_instances_[db_id] = db;
    db_registry_[db_id]["base_path"] = base_path;
    db_registry_[db_id]["data_path"] = data_path;
    return db;
}

// master_agent.cpp:81-100 — DbPathRequest handler
// Worker查询db路径 → Master从db_registry_查找返回
```

### 3.7 Worker注册消息 (当前无hostname)

```cpp
// message_types.h:47-58
struct RegisterMessage {
    uint64_t worker_id = 0;
    CMString role;
    CMVector<CMString> attributes;
    CMString data_server_host;   // ← 有host但用于数据传输端口
    int32_t data_server_port = 0;
    FLY_SERIALIZE(header, worker_id, role, attributes, data_server_host, data_server_port);
};
```

### 3.8 Worker启动 (Python侧)

```python
# fly/agent.py:222-250 — _spawn_process_worker()
cmd = [fly_bin, "--worker", "--worker-id", str(worker_id),
       "--master-host", self._host, "--master-port", str(self._port),
       "--log-dir", log_dir]
```

---

## 4. 设计方案 (已与用户讨论确认)

### 4.1 _DB_META 动态增量更新

**触发时机**: Worker写入完成 (DataReadyMessage) 到达Master时，检查该worker的host是否已在_DB_META中。

**写入方式**: 全量重写（非增量追加）。_DB_META文件小（10000 workers ≈ 1-5MB），全量写安全高效。使用tmp+rename保证原子性。

**更新条件**: 新写入数据的host不在_DB_META中，或worker_id不在对应host的list中。

**存储数据**:
```cpp
struct WorkerInfo {
    uint64_t worker_id = 0;
    CMString hostname;        // 机器唯一标识
    CMString ip_address;      // 网络通信地址
    CMString launch_command;  // Master生成的启动命令
    FLY_SERIALIZE(worker_id, hostname, ip_address, launch_command)
};

struct DbMeta {
    CMString db_id;
    CMString base_path;
    int64_t created_at = 0;
    int64_t updated_at = 0;    // ← 新增：最后更新时间，替代frozen_at用于非frozen场景
    CMVector<WorkerInfo> workers;
    FLY_SERIALIZE(db_id, base_path, created_at, updated_at, workers)
};
```

**关键变化**:
- 移除 `WorkerInfo` 中的 `role`, `data_path`, `idx_file`, `idx_entry_count`（可推导或不再需要）
- 新增 `hostname`, `ip_address`（用户确认需要两者）
- `DbMeta.updated_at` 替代 `frozen_at`（不再只在freeze时写）
- `launch_command` 由Master生成（当前仅支持local worker，未来扩展ssh/lsf）

### 4.2 load_db 接口

```python
# Python API
db = master.load_db("/path/to/db")
```

**执行流程**:
1. **读取_DB_META** → 获取 `{hostname → [worker_ids], hostname → ip, hostname → launch_command}`
2. **启动Worker** → 对每个hostname，调用 `launch_local_worker()` 或未来ssh/lsf，传入对应参数
3. **等待Worker注册** → 所有Worker通过RegisterMessage注册到Master，Master获得 `{new_worker_id → hostname, port}`
4. **发送idx加载命令** → Master向每个新Worker发送消息：`{需要加载的旧idx文件列表: ["worker_1.idx", "worker_3.idx"]}`
5. **Worker加载idx** → Worker从共享目录读取旧idx文件 → LocalIndex.load() → 恢复entries_ → 标记DataService中的entry为COMPLETE+flushed
6. **Worker创建新idx** → 新Worker使用新worker_id创建 `worker_{new_id}.idx`，后续写入新文件
7. **重建remote_idx** → Master读取所有旧idx → 重建 `{object_name → {new_worker_id, hostname, port}}` 到DataService::remote_idx_

### 4.3 idx共享前提

**用户确认**: idx文件一定存放在共享目录下（NFS/Lustre等），数据文件才可能存放在本地磁盘。

因此：
- 所有机器可通过共享目录访问 `{base_path}/worker_{id}.idx`
- 数据文件可能在本地磁盘（需要通过数据传输服务远程读取）

---

## 5. 待确认的设计决策

| # | 问题 | 建议方案 | 状态 |
|---|------|---------|------|
| 1 | hostname vs ip唯一性 | hostname作逻辑唯一键，ip供网络通信，两字段都存 | ✅ 用户确认 |
| 2 | _DB_META写入时机 | DataReadyMessage到达Master时检查并更新 | ✅ 用户确认 |
| 3 | _DB_META写入方式 | 全量重写 (tmp+rename保证原子性) | ✅ 用户确认 |
| 4 | launch_command来源 | Master生成，当前local worker，未来ssh/lsf | ✅ 用户确认 |
| 5 | hostname唯一标识 | hostname唯一性更强，作为机器标识 | ⚠️ 用户倾向hostname，需最终确认 |
| 6 | 新旧worker_id映射 | Master维护映射，发送旧idx文件列表给新Worker | ⚠️ 需确认 |
| 7 | idx加载消息类型 | 需新增消息类型(如IdxLoadMessage) | ⚠️ 需设计 |
| 8 | remote_idx重建时序 | 必须在所有Worker注册完成后重建 | ⚠️ 需确认 |
| 9 | WorkerInfo字段精简 | 移除role/data_path/idx_file/idx_entry_count | ⚠️ 需确认 |
| 10 | 完整+flushed标记 | load_db恢复的entry统一标记COMPLETE+flushed | ✅ 已验证代码时序 |

---

## 6. 实现需要修改的文件

### 新增
- `src/network/cpp/message_types.h` — 新增 `IdxLoadMessage`, `IdxLoadAckMessage`, `DbMetaUpdateMessage` 等
- `src/agent/cpp/master_agent.h/cpp` — 新增 `load_db()`, `update_db_meta()`, `rebuild_remote_idx()` 等方法
- `src/agent/cpp/worker_agent.h/cpp` — 新增 `load_foreign_idx()` 方法，处理idx加载消息
- `src/storage/cpp/db_meta.h` — 修改 WorkerInfo 和 DbMeta 结构
- `src/storage/cpp/database.h/cpp` — 新增 `_DB_META` 更新逻辑，新增从meta恢复DataService状态

### 修改
- `src/network/cpp/message_types.h` — RegisterMessage 增加 hostname 字段
- `src/agent/cpp/master_agent.cpp` — DataReady handler 中增加 _DB_META 更新逻辑
- `src/agent/cpp/worker_agent.cpp` — 注册时上报hostname
- `src/storage/cpp/data_service.h/cpp` — 新增 `restore_local_idx()`, `restore_remote_idx()` 方法
- `src/storage/export/storage_export.cpp` — 导出新的DbMeta操作接口
- `src/fly/agent.py` — 新增 `load_db()` Python API

---

## 7. 建议的下一步

1. **先确认第5-9项设计决策** — 与用户确认hostname唯一性、消息类型设计、字段精简等
2. **设计消息协议** — IdxLoadMessage/IdxLoadAckMessage的完整结构
3. **先写测试用例** — 按TDD流程，写load_db的集成测试
4. **分阶段实现**:
   - Phase 1: _DB_META增量更新 (WorkerInfo填充 + 全量重写)
   - Phase 2: load_db读取_DB_META + Worker启动
   - Phase 3: idx加载 + remote_idx重建
   - Phase 4: Python API整合

### 推荐使用的skills
- `writing-plans` — 实现前写详细计划
- `test-driven-development` — TDD流程
- `design-compliant-implementation` — 确保实现遵循设计约束

---

## 8. idx文件落盘时序验证 (已完成)

**验证结论**: idx文件中的记录一定对应已完全落盘的数据。

```cpp
// DataWriter::flush() — database.h 模板中的 execute 回调
file_stream_.flush();   // 1. 数据文件先刷盘
index_->save();         // 2. idx后刷盘

// complete回调在 flush 之后执行:
DataService::on_write_completed()  // → local_idx_ 标记 COMPLETE
DataService::on_object_flushed()   // → local_idx_ 标记 flushed
```

因此 `load_db` 恢复的 idx entry 可安全标记为 COMPLETE + flushed，不存在数据未落盘的窗口。

---

*文档生成时间: 2026-05-22*
*项目根目录: /root/fly*