# DB Merge 设计与实现方案

> 状态：**设计中**（未实现）
> 制定日期：2026-07-22（v3 — 对齐设计契约 + 主动 API）
> 关联：`docs/architecture.md` §3.3、§5.3、§5.4、§5.6；`docs/roadmap.md` §2.2-F2、§4 决策②；`docs/adr/0001-db-meta-and-load-db.md`

---

## 0. 摘要

提供 **`fly.merge_db(path)` 主动 API**：用户显式调用，把一个已 freeze 的 db 中分散在各 worker 本地磁盘（`data_path`）的 `.dat` 数据，**通过网络**聚合到 master 可达的 `base_path`（共享存储），产出一个**自包含、可独立加载**的合并数据库目录。

**核心设计依据**（`architecture.md §3.3` 的双路径契约）：
- `base_path`（**共享存储，必填**）：所有 Master/Worker 可访问，存 `_DB_META` / `_FROZEN` / `_VARS` / `<writer_id>.idx`。
- `data_path`（**本地磁盘，可选**）：仅写数据的进程可访问，存 `.dat`。不填则退化到 base_path。

因此：
- **索引/元数据天然全局可见**（在共享 base_path）—— master 可直接读所有 `<writer_id>.idx`，**不需要任何新消息类型**来传 idx。
- **真正本地化的是 `.dat` 数据本体**——这是 merge 要搬运的对象，必须走网络。
- **backup 机制已实现"跨机拉 `.dat` 字节 + 零解压落盘"的完整范式**，直接复用。

---

## 1. 存储模型（对齐设计契约）

### 1.1 双路径契约（`architecture.md §3.3`）

```python
db = open_db("/shared/project_a")                          # 仅共享路径
db = open_db("/shared/project_b", data_path="/ssd/local")  # 共享 + 本地（高性能写）
```

| 路径 | 说明 | 必填 | 存什么 | 多机可访问性 |
|------|------|------|--------|--------------|
| `base_path` | 共享存储路径 | 是 | `_DB_META`、`_FROZEN`、`_VARS`、`<writer_id>.idx` | ✅ 所有 Master/Worker |
| `data_path` | 本地磁盘路径 | 否 | `data_<wid>_<NNN>.dat`（压缩数据本体） | ❌ 仅写它的进程 |

代码印证（`data_writer.cpp:23,26`）：`.idx` 写 `base_path`，`.dat` 写 `data_path_.empty() ? base_path_ : data_path_`。
`_DB_META`/`_FROZEN`/`_VARS` 全部写 `base_path`（`database.cpp:475,558,573,874`）。

### 1.2 数据读取协议（`architecture.md §5.1`）

读对象走 5 层 fallback，**数据本体走网络**：
1. ObjectCache high（反序列化对象）
2. ObjectCache low（压缩字节）
3. 本地 `local_idx` → 本地 `.dat`
4. `remote_idx` → `DataClientPool.request()` **直连目标 Worker 的 DataServer（TCP）**
5. agent 层兜底（worker 问 master 拿位置，再直连）

DataServer（`data_server.cpp`）独立 epoll + send_thread_pool，`DATA_REQUEST`（msg_type=11）handler 调 `DataService::try_read_local_raw` 取本地 `.dat` 字节，`DataResponseProtocol` 两段式零拷贝回传。**与 task scheduler 完全解耦**——拉字节不占 task 执行槽。

### 1.3 merge 要解决什么

- **不是**索引聚合（索引已在共享 base_path，master 可直读全部 `<writer_id>.idx`）。
- **是**数据本体聚合：把分散在各 worker `data_path`（本地盘）的 `.dat`，搬到 master 可达位置，让数据库目录**自包含**——后续 `load_db` 无需原 worker 机器在线即可读取全部数据。

---

## 2. 复用 backup 范式（已验证的跨机数据搬运）

backup 是 fly 已实现的"跨 host 拉 `.dat` 字节零解压落盘"机制，merge 直接复用其原语。

### 2.1 backup 完整时序（参照）

```
触发端（3 入口殊途同归）
  ├─ 自动：master_agent.cpp:809 evaluate_and_trigger_backup（写完/读计数超阈值）
  └─ 手动：db.backup_object(name) → Database::backup_object（database.cpp:376）
       │
       ▼ 核心：跨机拉压缩字节
  DataService::read_raw_compressed(full_name)   data_service.cpp:932
    ├─ Tier-1 本地：try_read_local_raw → ObjectCache / local_idx → 本地 .dat
    └─ Tier-2 跨机：DataClientPool.request(host,port,name,...)
         ─── DATA_REQUEST(msg=11) ───▶ 源 worker DataServer
         ◀── DATA_RESPONSE(msg=12) + raw 压缩字节（两段式零拷贝）───
       │
       ▼ 落盘（零解压）
  Database::do_backup_write(full, name, compressed, hash)   database.cpp:325
    ├─ register_write（正常登记路径）
    ├─ writer_->write_record(... compressed_data ...)  ← 压缩字节直写 .dat，不解压
    └─ on_write_completed → local_idx / remote_idx 自动登记
```

### 2.2 merge 直接复用的原语

| 原语 | 位置 | 用途 |
|------|------|------|
| `DataService::read_raw_compressed(name)` | `data_service.cpp:932` | **拉单个对象压缩字节**（本地优先，自动跨机回退）。merge 拉数据的核心。 |
| `DataService::try_read_local_raw(name)` | `data_service.cpp:582` | 纯本地压缩字节读（DataServer 服务端也用它）。 |
| `Database::do_backup_write(full, name, data, hash)` | `database.cpp:325` | **把压缩字节落盘到目标 .dat + .idx**，走正常 DataWriter 路径，索引自动登记。 |
| `DataRequestMessage` / `DataResponseMessage` | `message_types.h:110,124` | 跨机拉单对象字节的消息（msg_type=11/12）。**无需新增**。 |
| `select_backup_worker(source)` | `master_agent.cpp:2021` | 选目标 worker 策略（跨机优先），merge 选源/目标可借鉴。 |

### 2.3 关键结论

- **拉单个对象字节**：现成 API（`read_raw_compressed`）。
- **拉一个 writer 的全部对象**：无批量协议，必须**逐对象循环**（先从共享 base_path 读该 writer 的 `.idx` 拿对象清单，再循环 `read_raw_compressed`）。
- **落盘**：现成 API（`do_backup_write`，零解压）。
- **`__backup_object` internal task 路径可选**：backup 的自动路径走 task scheduler（占一个 task 槽），手动路径（`db.backup_object`）不走 scheduler（进程内直连 DataServer）。**merge 选后者范式**——不占 task 槽，数据面直连。

---

## 3. merge_db API 设计

### 3.1 API 形态

```python
fly.merge_db(path: str, target_path: str = "", target_data_path: str = "") -> '_Database'
```

- **`path`**：源 db 的 `base_path`（共享存储，已 freeze）。
- **`target_path`**（可选）：合并产物的 `base_path`。默认 `path + ".merged"`。产物自包含（`_DB_META` + 全部 `.idx` + 全部 `.dat` 在此目录）。
- **`target_data_path`**（可选）：产物 `data_path`。默认空（`.dat` 写 target_path，纯自包含）。
- **返回**：合并后的 `_Database` 句柄（已 register，可直接 read）。
- **约束**：master-only（仿 `load_db`，只在 `Master` 类定义，`Worker` 调用自然 `AttributeError`）。

### 3.2 前置条件

- 源 db **必须已 freeze**（`is_db_frozen(db_id)` 或 `path/_FROZEN` 存在）。
  - freeze 保证：全员已 flush（`.dat` 落盘完整）、idx 稳定、无并发写。
  - 未 freeze → 抛 `RuntimeError("merge_db requires frozen db; call db.freeze() first")`。
- 源 db 的所有 writer 对应的 worker **当前在线**（merge 是"趁热聚合"，不是崩溃恢复）。
  - worker 离线 → 该 writer 的对象拉取失败 → **尽力而为**：跳过 + 在产物 `_DB_META` 记录缺失 writer 列表（见 §5-Q2）。

### 3.3 merge 流程（master 主导，4 阶段）

```
fly.merge_db(path, target_path, target_data_path)
   │
   ▼ Phase 1: 校验 + 读源 meta
   ├─ 检查 path/_FROZEN 存在（freeze 前置）
   ├─ 读 path/_DB_META：db_id + WorkerInfo 列表（writer_id, hostname, worker_id）
   └─ 在 target_path 创建空目标 Database（新 db_id 或复用源 db_id，见 Q1）
      → 写 target_path/_DB_META header
   │
   ▼ Phase 2: master 从共享 base_path 读全部 idx（无需网络）
   ├─ 对每个 WorkerInfo.writer_id：
   │    读 path/<writer_id>.idx（LocalIndex::load，已是共享 FS）
   │    → 收集 (writer_id → [IndexEntry]) 映射
   └─ 此时 master 持有"全局对象清单"：每个对象在哪个 worker、哪个 .dat 的 offset/size
   │
   ▼ Phase 3: 逐对象跨机拉 .dat 字节 → 落盘到 target（复用 backup 原语）
   ├─ 对每个对象 obj（按 writer 分组，便于日志/进度）：
   │    compressed = DataService::read_raw_compressed(obj.full_name)
   │      ├─ 本地命中（master 自写对象 / 已 cache）→ 直接用
   │      └─ 跨机 → DataClientPool → 源 worker DataServer → 回传压缩字节
   │    target_db.do_backup_write(obj.full_name, obj.short_name, compressed, hash)
   │      ├─ 零解压直写 target_path/<new_writer>.idx + target_data_path/*.dat
   │      └─ 自动登记 target 的 local_idx
   ├─ 失败处理：单个对象失败 → 记 missing_objects，继续（尽力而为）
   └─ 进度日志：每 N 个对象打印 progress
   │
   ▼ Phase 4: 收尾
   ├─ drain_write_back（确保 target 全部落盘）
   ├─ target_db.freeze()（产物是不可变的快照）
   ├─ 在 target_path/_DB_META 末尾追加 merge 完成标记：
   │    源 db_id、merge 时间、缺失 writer/对象列表（若有）
   └─ 返回 target_db 句柄
```

### 3.4 为什么不需要新消息类型

- **idx 传输**：master 从共享 `base_path` 直读 `<writer_id>.idx`（`LocalIndex::load`）。这是 load_db 已验证的路径（`master_agent.cpp:1772 rebuild_remote_idx_for_worker`）。
- **`.dat` 传输**：复用 `DATA_REQUEST/RESPONSE`（msg=11/12），通过 `read_raw_compressed`。
- architecture.md §5.3 设想的 `IdxRequest/IdxResponse`（msg=15/16）在双路径契约下**冗余**——索引已在共享盘。
  - 处置：保留枚举槽位（删会动 enum 数值），加注释标 reserved/unused（见 Q5）。

---

## 4. 实现触点

### 4.1 Python 层（主逻辑，仿 `Master.load_db`）

| 文件 | 改动 |
|------|------|
| `src/fly/__init__.py` | 定义 `merge_db(path, target_path="", target_data_path="")` 委托 `get_agent().merge_db(...)`；加 `__all__`（仿 `load_db` line 71-82, 190） |
| `src/agent/py/agent.py` | `Master` 类（line 98）加 `merge_db` 方法，实现 §3.3 的 4 阶段。**不动 `Worker` 类**（继承结构自然阻拦） |

### 4.2 C++ 层（暴露必要能力）

现有 C++ 接口已覆盖大部分需求（`agent_export.cpp:65-194` 的 `EXAgentMaster`）：
- `get_or_create_database(base_path, data_path, writer_id)` → 已有
- `is_db_frozen(db_id)` → 已有
- `register_database(db_id, base_path, data_path)` → 已有

**可能需新增**（取决于实现选择）：
| 文件 | 改动 | 必要性 |
|------|------|--------|
| `src/storage/cpp/database.{h,cpp}` | `read_raw_compressed` 若未对 master 进程暴露，加包装；或直接用 DataService 单例 | 中（master 进程也能调 DataService::instance()） |
| `src/storage/cpp/database.cpp` | `do_backup_write` 已是 private（`database.cpp:325`），merge 若跨 db 调用需暴露或新增 `merge_write` 公开接口 | 高 |
| `src/storage/export/storage_export.cpp` | 若新增 `Database::merge_write`，加 `FLY_EXPORT_METHOD`（仿 line 294 `freeze`） | 跟随上一项 |
| `src/agent/export/agent_export.cpp` | 若 `Master.merge_db` 需要新 C++ helper（如 `get_all_idx_entries(path)` 批量读 idx），加 `FLY_EXPORT_METHOD` | 视实现 |

### 4.3 不需要触碰

- **消息类型**（`message_types.h`）：不新增。复用 `DATA_REQUEST/RESPONSE`。
- **MessageProtocol / Reactor / DataServer**：泛型，自动适配。
- **fly.sh install / main.cpp**：不新增模块，现有 storage/agent .so 已挂载。
- **task scheduler**：merge 走数据面直连，不占 task 槽（仿 `db.backup_object` 手动路径）。

### 4.4 测试

| 类型 | 文件 | 内容 |
|------|------|------|
| 单元 | `src/storage/tests/database_test.cpp` | `do_backup_write` / `merge_write` 跨 db 落盘正确性 |
| QA（两阶段 run） | `qa/storage/test_merge_db.py`（仿 `qa/.../test_load_db.py`） | run1: 多 worker 各写本地 data_path → freeze；run2: `merge_db` → 校验产物自包含、可读全部对象、源 worker 不在线也能读 |

---

## 5. 开放问题（需确认）

### Q1. 产物 db_id：复用源还是新生成？

- **复用源 db_id**：产物与源同 id，object_name（`db_id:short`）不变，`load_db` 直接可用。但两个目录同 db_id 可能混淆。
- **新生成 db_id**：产物独立，object_name 需重映射（`old_db_id:short → new_db_id:short`），`do_backup_write` 时要改 full_name。复杂。
- **推荐**：复用源 db_id。产物视为源的"合并快照"，语义清晰。用户若需独立 id，复制目录后改 `_DB_META` header。

### Q2. 源 worker 部分离线的语义

- 尽力而为（推荐）：跳过离线 writer 的对象，产物 `_DB_META` 记录缺失列表。用户可后续对缺失部分重试。
- 全有或全无：任一离线即失败。过严，多机常态难满足。

### Q3. 进度反馈与中断恢复

- 大 db（百万对象）merge 耗时长。是否需要：
  - 进度回调（`on_progress(current, total)`）？
  - 中断恢复（写 `target_path/_MERGE_PROGRESS`，重跑跳过已完成对象）？
- **推荐**：一期只做进度日志（每 N 对象 INFO），不做恢复。复杂度留待真实痛点。

### Q4. 合并后源 db 的处置

- merge 不删源（源仍在各 worker 本地）。
- 用户若要清理源：手动删 `path` 目录 + 各 worker `data_path` 下对应 `.dat`（见 Q6 多机清理难题）。

### Q5. `IDX_REQUEST/IDX_RESPONSE`（msg=15/16）死枚举处置

- 双路径契约下确认冗余。
- **推荐**：保留槽位 + 加注释 `// reserved, unused — idx is on shared base_path, see db-merge-design.md`。不删（避免动 enum 数值）。

### Q6. 多机 data_path 物理位置不一致（清理难题）

- 各 worker 的 `data_path` 是各自机器本地路径（字符串可能相同，物理位置不同）。
- `_DB_META` 只记 `hostname`，不记每个 writer 的 `data_path` 字符串。
- **影响**：merge 产物自包含后，源数据清理需用户在各机器手动处理（fly 无法跨机删文件，除非新增协议）。
- **本方案不解决**：merge 只负责"聚合到 target"，源清理是运维操作。但需文档明确告知用户。

### Q7. 并发拉取

- 逐对象串行 `read_raw_compressed` 对大 db 慢。是否并发？
- `mapreduce.py:98 _mr_full_merge_task` 已用 `ThreadPoolExecutor` 并发读。
- **推荐**：一期可串行（简单，DataServer 自带 send_thread_pool 并发服务）；若慢再加 ThreadPoolExecutor 客户端并发。注意 `do_backup_write` 的线程安全（WriteBackQueue 是否支持并发 enqueue，需核实）。

---

## 6. 与现有文档的对齐（实现后修订）

| 文档 | 修订点 |
|------|--------|
| `docs/architecture.md` §5.3 | freeze 后处理：明确"不在 freeze 时自动聚合"；指向 `fly.merge_db` 主动 API |
| `docs/architecture.md` §3.3 | 补充 `merge_db` API 说明（base_path 共享契约下，data 聚合的网络路径） |
| `docs/architecture.md` §6.3 消息表 | `IDX_REQUEST/RESPONSE` 标 reserved/unused + 指向本文档 |
| `docs/roadmap.md` §五降级区 F2 | 从"降级"改为"主动 API 方案见 db-merge-design.md" |
| `docs/adr/` | 新增 ADR：记录"merge 复用 backup 网络范式 + 索引走共享 base_path，不引入新消息"的决策 |
| `CLAUDE.md` / `docs/storage/module.md` | 补充 merge_db API 与数据本地性说明 |

---

## 7. v1 → v2 → v3 变更说明

| 项 | v1 | v2 | v3（本版） |
|----|----|----|-----------|
| base_path 共享性 | 假设共享 | "场景相关"（含糊） | **设计契约：共享**（`architecture.md §3.3` 明确） |
| 真正本地化的对象 | 未区分 | idx + data 都可能本地 | **仅 `.dat`（data_path）本地**；idx/meta 在共享 base_path |
| idx 传输 | 判冗余 | 判必要（网络） | **冗余**（索引在共享盘，master 直读） |
| `.dat` 传输 | 未深入 | 提及 | **核心**，复用 backup 的 `read_raw_compressed` + `do_backup_write` |
| 触发时机 | freeze 自动 | freeze 自动/离线 | **用户主动 API**（`fly.merge_db`），不绑 freeze |
| 新消息类型 | 不新增 | 新增 IdxRequest/Response | **不新增**（复用 DATA_REQUEST/RESPONSE） |
| 方案重心 | 三层缺口 | 数据本地性 | **主动 API + 复用 backup 范式** |

---

*文档制定日期：2026-07-22（v3）*
*基于 `architecture.md §3.3` 双路径契约 + commit `e1aac14` 源码核实*
