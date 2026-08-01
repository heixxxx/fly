# ADR 0002: 废弃 db_id，改用 db_path + _MIGRATED_TO 迁移重定向

## 状态: 已确认 (2026-08-01)

## 背景

`db_id`（10 字符 base62）最初引入是为了缩短 db 唯一标识符、节省内存（早期 idx 每条记录存 `db_id:short` 全名）。经 2026-08-01 调研核实：

1. **内存层已良好归类**：`local_idx_`/`remote_idx_` 用嵌套 map，db_id 只作每 db 一份的外层 key，**不随对象数膨胀**。真正随对象数增长的内存项是 `LocalObjectInfo` 的 mutex+cv（~100B/对象）和 `remote_idx_` 无上限累积，与标识符无关（见 `docs/roadmap.md` [S1]）。

2. **磁盘层有冗余**：`LocalIndex` 是 per-(db,writer) 的，整个 .idx 文件天然属于同一 db，每条 entry 的 `object_name_` 带 db_id 前缀是 100% 冗余（每条 11 字节 × N，百万对象 ≈ 10MB）。

3. **db_id 的核心价值是逻辑锚点**：merge 改物理路径时让 object_name 保持稳定，保证跨 db 依赖链不断（如 solver build_matrix→merge→solve）。

## 决策

废弃 db_id，改用 **db_path（base_path）作 db 唯一标识符**，用"源 path 永久保留 + `_MIGRATED_TO` 迁移指针文件"替代 db_id 作为稳定锚点。

### 核心机制

1. **`_MIGRATED_TO` 迁移标识文件**：跨 path merge 时写在源 base_path，内容 `{target_base_path, target_data_path, migrated_at}`。源目录保留 `_DB_META`/`_FROZEN`/`_MIGRATED_TO`，删除 `.dat`/`.idx`。

2. **`DataService::resolve_migrated_path`**：单点重定向解析（链式展平 A→B→C，含缓存 `migrated_db_paths_`）。Database 构造时调用，跟随迁移到 target 的物理路径。

3. **db_path 合法性校验**：`open_db`/Database 构造时拒绝含 `:` 的 base_path → `full_name = "db_path:short"` 的 split 用 `rfind(':')` 永远无歧义。

### 渐进式实现策略

采用"db_id_ 作 base_path 别名"策略，分阶段废弃：
- `db_id_` 成员保留，但值从随机 10 字符改成 base_path（`full_name = db_id_ + ":" + short` 自然变成 `"db_path:short"`）
- 所有 map key、方法签名、网络消息字段类型不变（仍是 CMString），值语义变为 db_path
- 删除 `generate_db_id`（FNV-1a + 随机 + 碰撞检测）
- `split_full` 从固定偏移 10 改为 `rfind(':')`

这样全链路无需改签名/字段名，最小化改动面。

### solver build_matrix→merge→solve 链不断

- `get_full_name("matrix") = "db_path:matrix"`，用源 path 作 key
- merge 后源 path 仍存在（迁移指针）→ key 稳定 → DependencyGraph 匹配成功
- merge worker 读源对象、写产物；旧 db 句柄的 `db_paths_` 指向产物路径 → 读字节命中
- `set_paths` 不改 db_id_（保持源 path 作锚点），仅更新 base_path_/data_path_ 到产物

## ADR 0001 第 4 条作废

ADR 0001 第 4 条（db_id 持久化）的理由"搬目录后 idx 里 object_name 仍是 old 前缀 → mismatch"**已失效**：
- LocalIndex 改存 short_name 后，idx 不再编码任何 db 标识（阶段 1）
- 源 path 永久保留（迁移指针），通过它能找到最终数据

## 实施阶段

| 阶段 | 内容 | 状态 |
|------|------|------|
| 0 | 记录内存/磁盘问题到 roadmap.md [S1]/[S2] | ✅ |
| 1 | LocalIndex short_name 化（idx 不存 db_id 前缀） | ✅ |
| 2 | `_MIGRATED_TO` 迁移机制（resolve_migrated_path + 缓存） | ✅ |
| 3-4 | Database 废弃 db_id（别名策略）+ split 改 rfind | ✅ |
| 6 | merge 跨 path 接入 _MIGRATED_TO + cross_path QA | ✅ |
| 6 QA | test_merge_then_solve（solver 链不断） | ✅ |
| 后续 | 网络消息字段 rename、_DB_META 删 db_id、Python 全面清理 | 待定 |

## 验证

- C++ 单测 38/38 全过
- 全量 QA 137/138 全过（唯一失败的 test_locality_perf 是性能测试，原版也不稳定）
- 关键 QA：test_merge_db_cross_path（跨 path 迁移重定向）、test_merge_then_solve（solver 链不断）、test_load_db（含搬目录 Phase 3）

## 影响

- 消除 db_id 生成/碰撞检测/定长 split/db_id↔path 映射等复杂度
- idx 文件 name 部分约减 30-50%（S1-1）
- 标识符回归天然唯一（path）
- ADR 0001 第 4 条作废

---

*决策日期：2026-08-01*
