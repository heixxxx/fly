# DB元数据持久化与load_db恢复方案设计（已确认）

## 1. 当前状态

**项目**: Fly分布式任务框架 (C++20 + Python + nanobind)
**分支**: main, 最新commit: `fdbbd26`
**任务**: 设计并实现 `_DB_META` 动态增量更新 + `load_db` 恢复接口
**设计确认日期**: 2026-05-23

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

## 3. 设计决策汇总

### 3.1 数据结构变更

#### WorkerInfo（db_meta.h）

```cpp
// 旧结构
struct WorkerInfo {
    uint64_t worker_id = 0;
    CMString host;
    CMString role;
    CMString data_path;
    CMString idx_file;
    int64_t idx_entry_count = 0;
    CMString launch_command;
    FLY_SERIALIZE(worker_id, host, role, data_path, idx_file, idx_entry_count, launch_command)
};

// 新结构
struct WorkerInfo {
    uint64_t worker_id = 0;
    CMString hostname;        // Worker自行探测（gethostname），作为机器唯一标识
    CMString ip_address;      // Worker自行探测，辅助信息
    CMString launch_command;  // Master在launch_local_worker时生成
    FLY_SERIALIZE(worker_id, hostname, ip_address, launch_command)
};
```

**变更理由**:
- `role`: 未使用，移除
- `data_path`: DB级别已有，移除
- `idx_file`: 由 `base_path + "/worker_" + worker_id + ".idx"` 推导，移除
- `idx_entry_count`: 不再需要，移除
- `host` → `hostname`: 明确语义为机器标识
- 新增 `ip_address`: 网络通信辅助

#### DbMetaHeader（磁盘格式 header 部分）

```cpp
struct DbMetaHeader {
    CMString db_id;       // 持久化，load_db时直接使用，不重新计算hash
    CMString base_path;
    int64_t created_at = 0;
    FLY_SERIALIZE(db_id, base_path, created_at)
};
```

#### DbMeta（聚合返回值，非磁盘格式）

```cpp
// load_meta() 的返回值 — 从磁盘聚合而来
struct DbMeta {
    CMString db_id;
    CMString base_path;
    int64_t created_at = 0;
    CMVector<WorkerInfo> workers;  // 从增量记录聚合
};
```

#### RegisterMessage（message_types.h）

```cpp
// 新增 hostname 和 ip_address 字段
struct RegisterMessage {
    MessageHeader header;
    uint64_t worker_id = 0;
    CMString hostname;               // 新增：Worker机器hostname
    CMString ip_address;             // 新增：Worker机器IP
    CMVector<CMString> attributes;
    CMString data_server_host;       // 保留：数据传输服务器地址（与hostname不同）
    int32_t data_server_port = 0;    // 保留：数据传输端口
    static constexpr MessageType msg_type = MessageType::REGISTER;
    FLY_SERIALIZE(header, worker_id, hostname, ip_address, attributes, data_server_host, data_server_port);
};
```

**`data_server_host` 与 `hostname` 的区别**:
- `hostname`: 机器身份标识（用于 `_DB_META` 记录、load_db Worker 分配）
- `data_server_host`: 数据传输服务器可达地址（可能走专用网络，默认 `"127.0.0.1"`）

#### 新增消息类型

```cpp
// type=25: Master → Worker, 指示加载旧 idx 文件
struct IdxLoadCommandMessage {
    MessageHeader header;
    CMString db_id;
    CMString base_path;
    CMVector<uint64_t> old_worker_ids;  // Worker自行拼接 base_path/worker_{id}.idx
    static constexpr MessageType msg_type = MessageType::IDX_LOAD_COMMAND;
    FLY_SERIALIZE(header, db_id, base_path, old_worker_ids);
};

// type=26: Worker → Master, 确认 idx 加载完成
struct IdxLoadAckMessage {
    MessageHeader header;
    uint64_t worker_id = 0;
    CMString db_id;
    bool success = false;
    int32_t loaded_count = 0;
    CMString error_message;
    static constexpr MessageType msg_type = MessageType::IDX_LOAD_ACK;
    FLY_SERIALIZE(header, worker_id, db_id, success, loaded_count, error_message);
};
```

### 3.2 `_DB_META` 文件格式（增量追加）

```
磁盘格式:
[8 bytes header_size][bitsery-encoded DbMetaHeader{db_id, base_path, created_at}]
[8 bytes record_size][bitsery-encoded WorkerInfo{worker_id, hostname, ip, launch_command}]
[8 bytes record_size][bitsery-encoded WorkerInfo{...}]  ← 新 worker 写入时追加
...
```

**写入时机**:
1. **Database 构造时**: 写 header `{db_id, base_path, created_at=now}`
2. **on_data_ready() 中**: 检查内存缓存，若 `(hostname, worker_id)` 未记录 → 追加 WorkerInfo 到文件末尾 + 更新缓存
3. **freeze() 时**: 只写 `_FROZEN` 空文件（`_DB_META` 已最新）

**读取/聚合** (`load_meta()`):
1. 读 header → `{db_id, base_path, created_at}`
2. 逐条读后续 WorkerInfo 记录 → 聚合到 `workers` 向量
3. 返回完整 `DbMeta{header, workers}`

**内存缓存**（MasterAgent 中）:
```cpp
CMSet<std::pair<CMString, uint64_t>> recorded_workers_;  // (hostname, worker_id) 去重
CMMap<CMString, DbMeta> db_meta_cache_;                   // db_id → 已加载的 DbMeta
```

### 3.3 `db_id` 持久化

**问题**: 当前 `db_id` 由 `std::hash<CMString>{}(base_path_)` 生成。用户可能搬移 DB 目录后 `load_db`，路径变了但 idx 中所有 `object_name` 仍是 `"old_db_id:obj_name"` 格式。

**解决**: `db_id` 在 `_DB_META` header 中持久化。`load_db` 读取后调用 `set_db_id()` 覆盖 hash 值。

- `open_db`: 生成新 db_id（hash），写入 `_DB_META` header
- `load_db`: 从 `_DB_META` 读取 db_id，不重新计算

### 3.4 `open_db` 与 `load_db` 分离

| 接口 | 用途 | db_id 来源 | _DB_META |
|------|------|-----------|----------|
| `open_db(path)` | 创建新 DB | hash(path) | 创建新文件，路径冲突则报错返回 None |
| `load_db(path)` | 恢复已有 DB | 从 `_DB_META` 读取 | 读取现有文件 |

### 3.5 Master 启动约束

**⚠️ 当前版本要求 Master 在同一物理机上重启。**

跨机 Master 迁移是未来扩展项，需要解决：
- worker_0.idx 的数据文件本地性问题
- Master 自身数据远程读取服务

### 3.6 hostname 追踪

**存储位置**: MasterAgent

```cpp
CMMap<CMString, CMVector<uint64_t>> hostname_to_workers_;  // hostname → [worker_ids]
CMMap<uint64_t, CMString> worker_to_hostname_;             // worker_id → hostname
CMMap<uint64_t, CMString> worker_to_ip_;                   // worker_id → ip_address
```

**填充时机**: `on_worker_register()` 收到 RegisterMessage 时。

**用途**:
- `_DB_META` 增量更新（知道数据来自哪台机器）
- `load_db` Worker 分配（按 hostname 启动 Worker）
- `remote_idx` 重建（old_worker_id → hostname → new_worker_id）

### 3.7 worker_id 分配

**机制**: Master Python 层维护 `next_worker_id` 计数器。

```python
class Master:
    def __init__(self):
        self._next_worker_id = 1

    def launch_local_workers(self, ...):
        worker_id = self._next_worker_id
        self._next_worker_id += 1

    def load_db(self, path):
        max_old_id = max(all_old_worker_ids)  # 包括 master 的 0
        self._next_worker_id = max_old_id + 1
```

**避免冲突**: 新 Worker 的 writer_id 不会与旧 idx 文件重名。

### 3.8 统一写通知路径

**问题**: Master 写数据不发送 DataReadyMessage，导致 `_DB_META` 不更新、`remote_idx_` 不更新、dependency graph 不通知。

**解决**: 泛化 `WorkerAgentContext` → `AgentContext`，Master 构造 DataReadyMessage 直接调用 `on_data_ready()`。

```
统一后:
  Worker: write → callback → AgentContext::notify_write(db_id, obj_name)
                                → reactor_->send(DataReadyMessage)
                                → Master.on_data_ready()

  Master: write → callback → AgentContext::notify_write(db_id, obj_name)
                                → on_data_ready(0, DataReadyMessage{0, name, db_id})
                                ↕ 同一个 handler
```

**`on_data_ready()` 成为唯一的数据就绪处理入口**:
1. `remote_idx_` 更新
2. `_DB_META` 检查更新（新增逻辑只写这一处）
3. dependency graph 通知
4. `schedule_tasks()`

### 3.9 DataService 恢复 API

新增 `restore_entries()` 方法，Master 和 Worker 共用：

```cpp
void DataService::restore_entries(const CMString& db_id,
                                   const CMVector<IndexEntry>& entries) {
    // 按 object_name 分组 entries（大文件可能有多条 entry）
    // 写入 local_idx_，标记 COMPLETE + flushed
}
```

**Master 路径**: DataWriter 自动 load worker_0.idx → 提取 entries → `restore_entries()`

**Worker 路径**: 收到 IdxLoadCommand → 为每个 old_worker_id 创建独立 LocalIndex（只读）→ load → 提取 entries → `restore_entries()` → 丢弃 LocalIndex

### 3.10 Foreign idx 保护

Worker 加载的旧 idx 文件（如 worker_1.idx）为**只读**用途：
- 使用独立 `LocalIndex` 对象加载，不与 DataWriter 共享
- 提取 entries 后即丢弃
- Worker 的新写入走自己的 `worker_{new_id}.idx`（Database 构造函数自动创建）
- 不存在写入旧 idx 文件的路径

### 3.11 `wait_for_all_workers` 通用接口

```python
class Master:
    def wait_for_all_workers(self, count, timeout=30):
        """等待指定数量的 Worker 注册完成。供用户脚本和 load_db 共用。"""
```

---

## 4. 完整 load_db 流程

```
master.load_db("/path/to/db"):

Phase 1: 读取元数据
  1. 读 _DB_META → header{db_id, base_path, created_at} + WorkerInfo[]
  2. 聚合: {hostname → [old_worker_ids]}
  3. 收集所有 old_worker_ids (包括 master 的 0)

Phase 2: Master 自身恢复
  4. get_or_create_database(base_path, data_path, writer_id=0)
     → Database 构造 → DataWriter 自动 load worker_0.idx
  5. set_db_id(meta.db_id)  ← 覆盖 hash 值
  6. DataService: unregister(hash_id), register(meta.db_id, ...)
  7. 提取 worker_0.idx entries → restore_entries(db_id, entries) → local_idx_
  8. next_worker_id = max(old_ids) + 1

Phase 3: 启动 Worker
  9. 按 hostname 分组，排除 Master 自身 hostname
  10. 对匹配本机 hostname 的其他 worker_ids:
      launch_local_worker(next_worker_id)
      next_worker_id++
  11. wait_for_all_workers(count=needed_count)

Phase 4: 下发 idx 加载命令
  12. 对每个新 Worker:
      old_ids = _DB_META 中该 hostname 对应的 old_worker_ids
      send IdxLoadCommandMessage{db_id, base_path, old_ids}
  13. Worker 处理:
      for old_id in old_ids:
        LocalIndex(base_path + "/worker_" + old_id + ".idx").load()
        restore_entries(db_id, entries)
      reply IdxLoadAck{success, loaded_count}

Phase 5: 重建 remote_idx
  14. 等待所有 IdxLoadAck
  15. Master 读所有旧 idx 文件 → {object_name → old_worker_id}
  16. 映射链: old_worker_id → hostname (from _DB_META)
                         → new_worker_id (from RegisterMessage)
                         → data_server_host:port (from RegisterMessage)
  17. 重建 remote_idx_:
      for (object_name, old_worker_id) in all_entries:
        hostname = old_hostname_map[old_worker_id]
        new_worker = hostname_to_new_worker[hostname]
        remote_idx_[object_name] = {new_worker.id, host, port}
      Master 自身 entries:
      remote_idx_[name] = {0, master_data_server_host, master_data_server_port}
```

---

## 5. 已确认的设计决策清单

| # | 决策 | 结论 | 依据 |
|---|------|------|------|
| 1 | WorkerInfo 字段精简 | 移除 role/data_path/idx_file/idx_entry_count | 可推导或不再需要 |
| 2 | hostname vs ip | 两者都存：hostname 作唯一键，ip 辅助 | 集群环境 hostname 唯一性更强 |
| 3 | hostname 来源 | Worker 自行探测上报 | LSF 等系统 Master 无法预知 Worker 节点 |
| 4 | data_server_host 保留 | 保留，与 hostname 用途不同 | 可能走专用数据网络 |
| 5 | hostname 映射存储 | MasterAgent | 管理范畴，DataService 专注数据读写 |
| 6 | `_DB_META` 创建时机 | Database 构造时写 header | `created_at` 应记录真实创建时间 |
| 7 | `_DB_META` 格式 | 增量追加（header + WorkerInfo records） | 避免高频路径全量写开销 |
| 8 | `_DB_META` 更新触发 | `on_data_ready()` 统一处理 | Master 和 Worker 共用同一入口 |
| 9 | launch_command 来源 | Master 在 launch_local_worker 时生成 | Master 知道启动命令 |
| 10 | load_db Worker 启动 | 每 hostname 一个，当前仅本机 | 检测 hostname 匹配后 launch_local_worker |
| 11 | idx 加载消息 | IdxLoadCommand{db_id, base_path, old_worker_ids} | Worker 自行拼接文件名 |
| 12 | worker_id 分配 | next_worker_id 计数器 | load_db 时设 max(old_ids)+1 避免冲突 |
| 13 | Master 启动约束 | 必须同一 host | 文档记录，未来扩展跨机 |
| 14 | DataService 恢复 | restore_entries(db_id, entries) | Master 和 Worker 共用 |
| 15 | Foreign idx 保护 | 独立 LocalIndex 只读加载 | 不写入旧 idx 文件 |
| 16 | remote_idx 重建 | 所有 Worker 注册后，Master 从旧 idx 重建 | 需要 hostname → new_worker 映射 |
| 17 | Master 数据 in remote_idx | 是，指向 Master data_server | Worker 需远程读取 Master 数据 |
| 18 | 写通知统一 | AgentContext → on_data_ready() | 单一代码路径，易维护 |
| 19 | `db_id` 持久化 | `_DB_META` header 存储 | 搬移目录后 hash 会变 |
| 20 | `open_db` vs `load_db` | 纯创建 vs 纯恢复 | 职责分离 |
| 21 | idx 落盘时序 | 数据先刷 → idx 后刷 | 已验证，load_db 可安全标记 COMPLETE+flushed |

---

## 6. 实现文件清单

### 新增/修改

| 文件 | 变更类型 | 内容 |
|------|---------|------|
| `src/storage/cpp/db_meta.h` | 修改 | WorkerInfo 精简、新增 DbMetaHeader |
| `src/storage/cpp/database.h/cpp` | 修改 | `_DB_META` 创建拆分（构造时写 header）、load_meta() 改读增量格式 |
| `src/storage/cpp/data_service.h/cpp` | 修改 | 新增 `restore_entries()` |
| `src/network/cpp/message_types.h` | 修改 | RegisterMessage 加 hostname/ip、新增 IdxLoadCommand/Ack |
| `src/agent/cpp/master_agent.h/cpp` | 修改 | hostname 映射、`_DB_META` 缓存、on_data_ready 增强、load_db C++ 逻辑 |
| `src/agent/cpp/worker_agent.h/cpp` | 修改 | 注册时上报 hostname/ip、IdxLoadCommand handler |
| `src/agent/cpp/agent_context.h` | 新增 | 泛化 WorkerAgentContext → AgentContext |
| `src/storage/export/storage_export.cpp` | 修改 | 更新 DbMeta/WorkerInfo 导出 |
| `src/agent/export/agent_export.cpp` | 修改 | 导出新方法 |
| `src/fly/agent.py` | 修改 | next_worker_id、load_db、wait_for_all_workers |
| `src/fly/database.py` | 修改 | load_db 路径 |
| `src/fly/__init__.py` | 修改 | load_db 接口 |

---

## 7. 实现阶段

| Phase | 内容 | 依赖 |
|-------|------|------|
| **Phase 1** | 数据结构 + 消息变更 | 无 |
| | WorkerInfo/DbMetaHeader 改造 | |
| | RegisterMessage 加 hostname/ip | |
| | IdxLoadCommand/Ack 新增 | |
| | AgentContext 泛化 | |
| **Phase 2** | `_DB_META` 增量更新 | Phase 1 |
| | Database 构造时写 header | |
| | `on_data_ready()` 中追加 WorkerInfo | |
| | MasterAgent hostname 映射 + 缓存 | |
| | MasterAgent `on_local_write_completed()` | |
| **Phase 3** | DataService 恢复 | Phase 1 |
| | `restore_entries()` | |
| | Worker IdxLoadCommand handler | |
| | Foreign idx 只读加载 | |
| **Phase 4** | load_db 完整流程 | Phase 2 + 3 |
| | Python next_worker_id | |
| | Master load_db (自身恢复 + Worker 启动 + idx 下发) | |
| | remote_idx 重建 | |
| | wait_for_all_workers | |
| **Phase 5** | 测试 + 文档 | Phase 4 |
| | 单元测试 (db_meta, restore_entries, idx load) | |
| | 集成测试 (load_db E2E) | |
| | CLAUDE.md 更新 | |

---

## 8. idx文件落盘时序验证（已完成）

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
*设计确认时间: 2026-05-23*
