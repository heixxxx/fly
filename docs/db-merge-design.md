# DB Merge（Freeze 后处理）设计与实现方案

> 状态：**设计中**（未实现）
> 制定日期：2026-07-22
> 关联：`docs/architecture.md` §5.3、§5.6；`docs/roadmap.md` §2.2-F2、§4 决策②、§五降级区；`docs/adr/0001-db-meta-and-load-db.md`

---

## 0. 摘要

**DB Merge = Database Freeze 后处理**。当前 `db.freeze()` 只完成"打标记 + 刷盘 + 通知各方打标记"，
跨 worker / 跨 writer 的聚合产物**完全未实现**。本文档基于 2026-07-22 源码核实给出：

1. 现状缺口的三层精确定义（§2）
2. load_db 已具备的恢复能力，以及它为何是本设计的核心约束（§3）
3. 分阶段实现方案 + 每阶段代码触点（§4）
4. 必须先拍板的开放问题（§5）

**核心结论**：architecture.md §5.3 设想的 `IdxRequest/IdxResponse`（走消息体传 idx 内容）在当前
"共享文件系统"架构下是**冗余且更复杂**的路径——`IdxLoadCommand`（load_db 在用）已证明
"master/worker 都能直接读 `base_path/<writer_id>.idx`"是可行且更简单的范式。本方案不新增
`IdxRequest/IdxResponse`，改为复用共享 FS。

---

## 1. 概念地图（术语对齐）

| 术语 | 实体 | 粒度 | 谁持有 |
|------|------|------|--------|
| `local_idx_` | worker 自己写的对象索引（内存） | per (db_id, worker) | Worker 的 `DataService` |
| `<writer_id>.idx` | 上述索引的磁盘镜像（操作日志格式） | per writer | `base_path/<writer_id>.idx` |
| `remote_idx_` | master 的全局对象→worker 位置索引（内存，从不落盘） | per (db_id, object) | Master 的 `DataService` |
| `_DB_META` | db 元数据（header + WorkerInfo 追加日志） | per db | `base_path/_DB_META` |
| `_FROZEN` | 空标记文件 | per db | freeze 时创建 |
| `_VARS` | 非删除 vars 的持久化 | per db | freeze 时写 |
| `merged.idx` | **架构文档设想的** freeze 聚合索引产物 | per db | **未实现** |
| `_META` | **架构文档设想的** freeze 后 db 元信息聚合 | per db | **未实现，与 `_DB_META` 区分** |

> ⚠️ 命名陷阱：architecture.md §5.3 写的"写入 `_META`"与已有的 `_DB_META` 不是同一文件。
> `_DB_META` 是 write 期增量追加的 writer 登记表（ADR-0001），freeze 不改它。
> `_META`（设想）是 freeze 后的聚合产物。**建议实现时直接复用/收尾 `_DB_META`，不新引入 `_META`**，避免双元数据源（见 §5-Q1）。

---

## 2. 现状缺口（三层，经源码核实）

### 2.1 缺口 A：idx 未 compact（轻量，已有半成品）

- `LocalIndex::compact()`（`src/storage/cpp/local_index.cpp:327`）**已实现但无任何生产调用方**。
  产物是把操作日志（BEGIN/ADD/END/REMOVE）重写为无标记的干净 ADD 段外条目。
- freeze 路径（`database.cpp:390-412`）未调 compact。
- 影响：`<writer_id>.idx` 含历史 REMOVE/BEGIN/END 噪声，体积膨胀，load 读放大。

### 2.2 缺口 B：`removed_objects_` 的 `.dat` 物理数据未回收（中量，有 TODO）

- `database.cpp:404-411` 显式 TODO：freeze 只记日志，聚合 `.dat` 文件中被 `remove_object()` 删掉的
  对象数据仍占空间。
- `.dat` 结构（`data_writer.cpp:35`）：`data_<writer_id>_<NNN>.dat`，多对象追加到一个文件，
  `IndexEntry.offset_/size_` 定位。删除单对象需重写整个 `.dat` → 即 compaction。
- 影响：长期 run + 频繁 remove 时磁盘膨胀。

### 2.3 缺口 C：跨 worker idx 聚合 / `merged.idx` / `_META`（重量，完全空白）

- architecture.md §5.3 设想的"master 收集所有 worker idx → 合并 → 写 `merged.idx` + `_META`"
  **零实现**。
- `MessageType::IDX_REQUEST=15` / `IDX_RESPONSE=16`（`message_types.h:24-25`）**仅有枚举槽位，
  无 struct、无 handler、无 register**。
- master 注册的 idx 相关 handler 只有 `IdxLoadAckMessage`（`master_agent.cpp:132`），属 load_db 范式。

### 2.4 freeze 实际做了什么（已实现，供对照）

`Database::freeze()`（`database.cpp:390-412`）：
1. `drain_write_back()` —— 刷空 WriteBackQueue
2. `is_frozen_ = true`
3. `create_frozen_marker()` —— 写空文件 `_FROZEN`
4. `on_flush(db_id)` —— local_idx 全部对象标 `flushed_=true`
5. `cleanup_temp_entries(db_id)` —— 清 `is_temp_=true` 临时对象
6. `flush_vars_to_disk()` —— 写 `_VARS`
7. **NOT DONE**：removed_objects_ 物理回收 / idx compact / 跨 worker 聚合

---

## 3. load_db：本设计的第一约束（为何不能照搬 §5.3）

### 3.1 load_db 当前如何恢复索引（ADR-0001，已落地）

`agent.py:276 load_db` → 不依赖任何 `merged.idx`：

1. **Phase 1**：读 `_DB_META`（`agent.py:286-298`）拿 `db_id` + `WorkerInfo` 列表，注册 db paths，
   **不加载任何 idx**。
2. **Phase 2**：按 `hostname` 分组 `meta.workers` → `writer_ids`（`agent.py:303-305`）。
   缺 worker 的 hostname → spawn 新 worker 传 `--host`（`agent.py:314-323`）。
3. **Phase 3**：对每个 hostname，`send_idx_load_to_worker(db_id, path, writer_ids, worker_id)`
   （`agent.py:340`）→ worker 读 `base_path/<writer_id>.idx` 填本地 `local_idx_`
   （`worker_agent.cpp:1033-1081`）。
4. **Phase 4**：master 收 `IdxLoadAck` 后，**自己从同一共享 FS 重开每个 `<writer_id>.idx`**
   重建 `remote_idx_`（`master_agent.cpp:1795-1813` → `rebuild_remote_idx_for_worker:1765-1793`）。

### 3.2 关键推论

- **load_db 已经能正确恢复全部索引**，前提是：每个 `writer_id` 的 `.idx` 文件在**某台可达机器上**，
  且该机器上有对应 hostname 的 worker。
- 这正是 roadmap §4 决策②把 F2 降级的理由原文："当前 `load_db` 在 worker 齐备时能正确加载全部索引，
  freeze 聚合非阻塞需求。"
- 因此 **`merged.idx` 的唯一独立价值**是：当某个 writer 的机器**不可达 / 无 worker**时，
  提供一份不需要回源到原始 writer 机器的聚合索引。在所有 writer 机器都健在的常规场景下，
  `merged.idx` 与"load_db 重读各 `<writer_id>.idx`"是等价的，后者已可用。

### 3.3 对本设计的约束

任何 freeze 后处理方案**必须**：
- 不破坏 load_db 的现有路径（load_db 读的是 `<writer_id>.idx`，不能让 freeze 把它删掉/改坏）
- 明确 `merged.idx` 是"补充"还是"替代"各 `<writer_id>.idx`（见 §5-Q2）
- 决定 freeze 聚合是**在线**（freeze 时同步做，增加 freeze 延迟）还是**离线**（独立命令/工具）

---

## 4. 分层实现方案（独立可交付）

> 三层缺口相互独立，可分别交付。建议优先级：**A（低风险高收益）→ B（中）→ C（仅在有跨机不可达痛点时）**。

### 4.1 方案 A：freeze 触发 idx compact（缺口 A，推荐先做）

**目标**：freeze 时把 `<writer_id>.idx` 从操作日志格式压成干净快照，缩小体积、消除 load 读放大。

**改动点（2 处 + 测试）**：
- `database.cpp:freeze()`（line 390-412）：在 `drain_write_back()` 之后、`on_flush()` 之前，对本 db 的
  每个 writer 调 `LocalIndex::compact()`。`LocalIndex` 句柄获取路径见 `data_writer` / `DataService`
  已有的 per-writer index 持有。
- 测试：`src/storage/tests/local_index_test.cpp` 已覆盖 compact 语义；新增
  `database_test.cpp` 用例：freeze 前写+删若干对象 → freeze → 断言 idx 文件无 REMOVE/BEGIN/END 标记
  且能被 `LocalIndex::load()` 正确读回全部存活对象。

**风险**：低。`compact()` 用 `.compact` 临时文件 + `rename` 原子替换（`local_index.cpp:328,352`）。
需确认 freeze 期无并发写（freeze 本就拒绝后续 write，`check_frozen()`，安全）。

**与 load_db 兼容性**：✅ compact 后的 idx 仍是合法的 `<writer_id>.idx`，load_db 的
`LocalIndex::load()` 能正常读（旧格式回退分支 `local_index.cpp:170-192` 处理历史格式，
compact 产出的干净格式是 load 的主路径）。

### 4.2 方案 B：freeze 触发 `.dat` compaction（缺口 B，回收 removed_objects_）

**目标**：freeze 时把被 `remove_object()` 标记的对象数据从 `.dat` 物理删除，重写紧凑 `.dat`。

**设计要点**：
- 遍历 compact 后的 idx 中存活对象的 `(file_name, offset, size)`，按 `.dat` 分组。
- 对每个 `data_<writer_id>_<NNN>.dat`：新建临时 `.dat`，按 offset 升序拷贝存活段，
  重建对象→新 offset 映射，写新 idx（指向新 `.dat`），原子 rename。
- `IndexEntry.host_` / `write_context_hash_` 保留不变。
- 删除孤儿 `.dat`（若某 `.dat` 全部对象都被 remove）。

**改动点（重量）**：`database.cpp` 新增 `compact_dat_files()`，在 freeze 的 compact idx 之后调用；
需协调 `DataWriter` 暴露 dat 文件枚举/重建能力。测试覆盖：多对象混合 remove 后磁盘占用下降 +
load 仍能读全部存活对象。

**风险**：中。重写 `.dat` 是 I/O 密集操作，大 db freeze 延迟显著增加。**建议加配置开关**
`freeze_compact_dat`（默认关或仅当 `removed_objects_` 占比超阈值时触发），见 §5-Q3。

**与 load_db 兼容性**：✅ 只要新 `.dat` + 新 idx 自洽，load_db 无感知。

### 4.3 方案 C：跨 worker idx 聚合 → `merged.idx`（缺口 C，仅跨机不可达痛点时）

**目标**：freeze 后产出一份聚合索引，使 load_db 在某 writer 机器不可达时仍能恢复（部分）索引。

#### 4.3.1 为什么不照搬 architecture.md §5.3 的 IdxRequest/Response

§5.3 设想：master 向 worker 发 `IdxRequest` → worker 回 `IdxResponse`（idx 内容塞进消息体）→ master 合并。

**问题**：
1. **冗余**：master 和所有 worker 都挂同一共享 FS（ADR-0001 前提，load_db 的全部可行性建立在此）。
   master 直接读 `base_path/<writer_id>.idx` 即可（`rebuild_remote_idx_for_worker` 已这么做），
   无需经网络搬 idx 字节。
2. **消息体过大**：大 db 的 idx 可达数百 MB，塞进单条消息违背两段式 DataResponse 协议的设计初衷。
3. **已有枚举但无实现的死代码**：`IDX_REQUEST/RESPONSE` 是架构债，实现它等于把债坐实。

#### 4.3.2 推荐方案 C-1：master 本地聚合（复用共享 FS）

**流程**（freeze 后处理，master 侧）：
1. freeze 广播完成后，master 遍历 `_DB_META` 的全部 `WorkerInfo`。
2. 对每个 `writer_id`，从共享 FS 读 `base_path/<writer_id>.idx`（已是方案 A compact 后的干净格式）。
3. 合并所有 entries 到单文件 `base_path/merged.idx`（格式 = 多个 AddRecord 顺序排列，
   与 compact 产物同构，便于 load 时复用 `LocalIndex::load()` 解析逻辑）。
4. 收尾 `_DB_META`：在末尾追加一条 freeze 完成标记记录（或在 `_DB_META` header 加 frozen_at 字段），
   **不新建 `_META` 文件**（见 §5-Q1）。

**优点**：零新消息类型、零网络传输、复用 `LocalIndex` 的 encode/decode。
**前置假设**：所有 writer 的 `.idx` 在 master 可达的共享 FS 上（当前架构已保证）。

#### 4.3.3 load_db 如何消费 `merged.idx`（关键改动）

`agent.py:load_db` Phase 3 增加 fallback：
```
for hostname, writer_ids in hostname_to_writer_ids.items():
    workers = existing_by_hostname.get(hostname, [])
    if workers:
        # 常规路径：定向加载该 host 的 writer_ids
        send_idx_load_to_worker(...)
    else:
        # 回退路径：该 host 无 worker（机器下线）→ 从 merged.idx 补该 host 的索引
        # master 侧直接从 merged.idx 过滤出这些 writer_id 的 entries，
        # restore 到 remote_idx_（数据本体 .dat 仍需该 host 可达才能真读，
        #   但至少索引可见、依赖图可解析、调度可决策）
```
**边界**（必须明确，见 §5-Q4）：`merged.idx` 只恢复**索引**，不恢复**数据本体**。若某 writer 机器
彻底不可达，其 `.dat` 读不到，读该对象仍会失败。聚合索引的价值限于"依赖图可见性 / 调度决策 /
对象存在性查询"，不是数据可用性。

#### 4.3.4 替代方案 C-2（仅记录，不推荐）

照搬 §5.3 实现 `IdxRequest/Response`。仅当未来架构放弃共享 FS（纯 message-passing 多机）时才需考虑。
当前是过度设计。

---

## 5. 开放问题（需确认）

> 以下为影响实现选型的决策点。本设计文档给出推荐倾向，但需明确裁定。

### Q1. `_META` vs `_DB_META`：新建还是收尾？

- architecture.md §5.3 写"写 `_META`"，但 `_DB_META` 已是 write 期增量追加的 writer 登记表（ADR-0001）。
- **推荐**：不新建 `_META`，在 `_DB_META` 末尾追加 freeze 完成标记（或在 header 加 `frozen_at`）。
  理由：单一元数据源，避免 `_DB_META` 与 `_META` 漂移；load_db 已读 `_DB_META`，零额外读路径。

### Q2. `merged.idx` 与各 `<writer_id>.idx` 的关系：并存还是替代？

- **并存（推荐）**：freeze 产出 `merged.idx`，但保留各 `<writer_id>.idx`。load_db 优先用定向
  `IdxLoadCommand`（数据 + 索引双恢复），`merged.idx` 仅作"某 writer 机器不可达"时的索引 fallback。
- **替代**：freeze 后只留 `merged.idx`，删各 `<writer_id>.idx`。**不推荐**：破坏 load_db 主路径，
  且 `merged.idx` 不含 `.dat` 数据本体，无法替代定向加载的数据恢复能力。

### Q3. freeze compaction 是否加配置开关 / 阈值？

- 方案 B（`.dat` compaction）对大 db 是重 I/O。
- **推荐**：加 `freeze_compact_dat`（默认 0=关）+ 触发阈值（如 removed 占比 > 20% 才做）。
  方案 A（idx compact）轻量，默认开。

### Q4. `merged.idx` 的数据可用性边界（需对用户文档化）

- 必须明确：**`merged.idx` 只恢复索引，不恢复数据本体**。某 writer 机器不可达时，其对象索引可见
  但读取仍失败。是否接受此边界？还是要求 freeze 时连带把 `.dat` 也聚合到 master 本地
  （=数据副本/备份，量级远大于索引，接近现有 backup 机制）？

### Q5. freeze 后处理：在线（同步）还是离线（独立命令）？

- **在线**：在 `freeze()` / master freeze handler 里同步做。增加 freeze 延迟，但语义简单（freeze 返回即聚合完成）。
- **离线**：提供独立工具/命令（如 `db.merge(path)` 或 CLI），freeze 只打标记，聚合由用户显式触发。
- **推荐**：方案 A 在线（轻）；方案 B/C 离线或配置开关，避免拖慢 freeze 热路径。

### Q6. 与现有 backup 机制的关系

- 已有 backup（`docs/architecture.md` §5.4）：跨 host 复制压缩数据字节，是**数据级**冗余。
- 方案 C 的 `merged.idx` 是**索引级**聚合，与 backup 正交。若 backup 已保证数据多副本，
  `merged.idx` 的跨机不可达价值是否还有必要？（可能 backup 健全时方案 C 不需要做）

### Q7. `IDX_REQUEST/IDX_RESPONSE` 枚举槽位（15/16）的处置

- 当前是死代码（仅枚举，无实现）。方案 C-1 不需要它们。
- **推荐**：保留枚举槽位（删了会动 enum 数值，影响协议兼容），但在注释标注"reserved, unused"，
  或在本文档记录其为架构债。是否现在清理？

---

## 6. 建议交付顺序

| 阶段 | 内容 | 风险 | 前置条件 |
|------|------|------|----------|
| **P0** | 方案 A：freeze idx compact | 低 | 无；独立可交付 |
| **P1** | 方案 B：freeze `.dat` compaction（带开关） | 中 | Q3 裁定 |
| **P2** | 方案 C-1：master 本地聚合 `merged.idx` + load_db fallback | 中 | Q1/Q2/Q4/Q6 裁定；且确认有跨机不可达真实痛点 |

> 若 §5 各问题裁定为"backup 已足够、无跨机不可达痛点"，则方案 C 可不做，
> roadmap F2 维持降级状态，仅交付 P0（+可选 P1）。

---

## 7. 受影响文件预估（按阶段）

### 方案 A
- `src/storage/cpp/database.cpp` — freeze() 调 compact
- `src/storage/tests/database_test.cpp` — 新增 freeze+compact 用例

### 方案 B
- `src/storage/cpp/database.{h,cpp}` — `compact_dat_files()`
- `src/storage/cpp/data_writer.{h,cpp}` — dat 枚举/重建能力
- `src/core/cpp/config.cpp` — `freeze_compact_dat` 开关
- 测试：`database_test.cpp` / 新 `compaction_test.cpp`

### 方案 C-1
- `src/storage/cpp/database.cpp` 或 master 侧 — 聚合写 `merged.idx`
- `src/storage/cpp/local_index.{h,cpp}` — 复用 load/encode，可能加 `load_merged()`
- `src/agent/py/agent.py:load_db` — Phase 3 加 merged.idx fallback
- `src/storage/cpp/db_meta.{h,cpp}` — freeze 完成标记（若 Q1 选收尾 `_DB_META`）
- 测试：`master_agent_test.cpp`（聚合流程）+ `agent` 侧 load_db fallback 用例

### 新增消息类型
- 方案 A/B/C-1 **均不需要**新消息类型（C-1 复用共享 FS，不走消息体）。
- 仅当未来选 C-2 才需激活 `IDX_REQUEST/RESPONSE`（不推荐）。

---

## 8. 与现有文档的对齐（实现后需同步修订）

| 文档 | 修订点 |
|------|--------|
| `docs/architecture.md` §5.3 | 标注 freeze 后处理已实现哪些层（A/B/C）；澄清 `_META` 实为 `_DB_META` 收尾 |
| `docs/architecture.md` §6.3 消息表 | `IDX_REQUEST/RESPONSE` 标注"reserved/unused"或删除 |
| `docs/roadmap.md` §五降级区 F2 | 按实际交付更新状态（P0 完成→部分解降级；或维持降级） |
| `docs/adr/` | 若选定 C-1，新增 ADR 记录"为何不实现 IdxRequest/Response 而复用共享 FS" |
| `CLAUDE.md` / `docs/storage/module.md` | freeze 流程描述更新 |

---

*文档制定日期：2026-07-22*
*基于 commit `e1aac14` 的源码核实*
