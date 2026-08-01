# ADR 0001: DB元数据持久化与load_db恢复方案

## 状态: 已确认 (2026-05-23)

## 背景

Fly 框架的 `open_db` 创建的 Database 在进程退出后无法恢复。虽然数据文件 (`.dat`) 和索引文件 (`.idx`) 已持久化到磁盘，但 `DataService` 的内存索引 (`local_idx_`, `remote_idx_`, `db_paths_`) 在新进程中为空，导致所有读取操作失败。

## 决策

### 1. `_DB_META` 增量追加格式

**选择**: 增量追加格式（header + WorkerInfo records），而非全量重写。

**理由**:
- `_DB_META` 更新发生在 `on_data_ready()` 高频路径上，全量重写开销过大
- 参照 `LocalIndex` 的增量追加模式，保持架构一致
- `load_meta()` 时聚合返回完整 `DbMeta` 结构

**格式**: `[8B header_size][bitsery DbMetaHeader] [8B record_size][bitsery WorkerInfo]...`

### 2. 统一写通知路径 (AgentContext)

**选择**: 泛化 `WorkerAgentContext` → `AgentContext`，Master 构造 `DataReadyMessage` 直接调用 `on_data_ready()`。

**替代方案**: Master 通过 Reactor loopback 发送消息；Master 独立处理逻辑。

**理由**:
- 单一代码路径：`on_data_ready()` 是唯一的数据就绪处理入口
- 未来变更只需改一处（`_DB_META` 更新、`remote_idx_`、dependency graph）
- Master 和 Worker 的写路径完全对称

### 3. Master 同一 Host 约束

**选择**: 当前版本要求 Master 在同一物理机上重启。

**理由**: 
- `worker_0.idx`（Master 数据）的数据文件可能在本地磁盘
- 跨机启动时 Master 无法判断是否应加载 `worker_0.idx`
- 简化初始实现，避免 hostname 匹配的复杂逻辑

**未来扩展**: 支持跨机 Master 迁移需要解决 worker_0 数据本地性问题。

### 4. `db_id` 持久化

> ⚠️ **已作废（2026-08-01，见 [ADR 0002](0002-deprecate-db-id.md)）**：db_id 已废弃，改用 db_path + `_MIGRATED_TO` 迁移重定向。LocalIndex 改存 short_name 后 idx 不再编码 db 标识，"搬目录导致 mismatch"的理由失效。`_DB_META` 的 db_id 字段待后续清理阶段移除。

**选择**: `db_id` 存储在 `_DB_META` header 中，`load_db` 直接读取使用。

**理由**: 用户可能搬移 DB 目录到新路径，路径 hash 生成的 `db_id` 会变化，但 idx 中所有 `object_name` 仍是 `"old_db_id:obj"` 格式，导致 mismatch。

### 5. `next_worker_id` 计数器

**选择**: Python 层维护全局计数器，`load_db` 时设为 `max(old_ids) + 1`。

**理由**: Worker 构造函数自动创建 `worker_{id}.idx`，新 id 不能与旧 id 冲突。

## 影响的消息协议

| 消息 | 变更 |
|------|------|
| RegisterMessage | 新增 `hostname`, `ip_address` 字段 |
| IdxLoadCommandMessage (type=25) | 新增 |
| IdxLoadAckMessage (type=26) | 新增 |

## 受影响的文件

- `db_meta.h` — WorkerInfo 精简、DbMetaHeader 新增
- `message_types.h` — RegisterMessage 扩展 + 2 新消息
- `database.h/cpp` — `_DB_META` 创建拆分
- `data_service.h/cpp` — `restore_entries()` 新增
- `master_agent.h/cpp` — hostname 映射、`_DB_META` 缓存、写通知
- `worker_agent.h/cpp` — hostname 上报、IdxLoad handler
- `agent.py` — next_worker_id、load_db、wait_for_all_workers
