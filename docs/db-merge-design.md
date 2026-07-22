# DB Merge（Freeze 后处理）设计与实现方案

> 状态：**设计中**（未实现）
> 制定日期：2026-07-22（v2 — 修正数据本地性前提）
> 关联：`docs/architecture.md` §5.3、§5.6；`docs/roadmap.md` §2.2-F2、§4 决策②、§五降级区；`docs/adr/0001-db-meta-and-load-db.md`

---

## 0. 摘要

**DB Merge = Database Freeze 后处理**。当前 `db.freeze()` 只完成"打标记 + 刷盘 + 通知各方打标记"，
跨 worker / 跨 writer 的聚合产物**完全未实现**。

**v2 关键修正**（相比 v1 草案）：fly 多机运行时每个 worker 把 `.idx` + `.dat` 写到**本进程/本机本地路径**
（`data_writer.cpp:23`，`data_path` 可独立于 `base_path`），数据读取走 **DataServer TCP 网络协议**
（`data_server.cpp`，非共享文件系统）。因此本方案 v1 基于"共享 FS"的推论不成立，已重写。

本文档给出：
1. 数据本地性模型（§1，方案的物理基础）
2. 现状缺口的三层定义（§2）
3. load_db 的现有能力与局限（§3，受本地性约束）
4. 修正后的分阶段方案（§4）
5. 必须先拍板的开放问题（§5）

---

## 1. 数据本地性模型（方案的物理基础，必须先对齐）

### 1.1 谁写在哪里

| 产物 | 写入位置 | 由谁写 | 多机时物理位置 |
|------|----------|--------|----------------|
| `_DB_META`（writer 登记表） | `base_path` | Database 构造时写 header（`database.cpp:579`），master 在 task 完成时追加 WorkerInfo（`master_agent.cpp:802`） | **base_path 是否共享见 §1.3** |
| `_FROZEN` / `_VARS` | `base_path` | freeze 时写（`database.cpp:558,573,874`） | 同上 |
| `<writer_id>.idx` | `base_path` | DataWriter 构造（`data_writer.cpp:26`） | **base_path 是否共享见 §1.3** |
| `.dat`（`data_<wid>_<NNN>.dat`） | `data_path_.empty() ? base_path_ : data_path_`（`data_writer.cpp:23`） | DataWriter::write_record | **写数据的 worker 本地**（`open_db(path, data_path)` 的 data_path 常指向本地盘） |

### 1.2 谁读谁（网络 vs 文件系统）

- **数据本体（`.dat`）**：远程读走 **DataServer TCP**（`data_server.cpp` 监听 socket + epoll + send_thread）。
  `DataService` 的 `remote_idx_` 记录每个对象的 `(worker_id, host, port)`，读时连对应 worker 的 DataServer 拉压缩字节。
  **从不依赖共享 FS 读 `.dat`**。
- **索引（`.idx`）/ 元数据（`_DB_META`）**：load_db 通过 `IdxLoadCommandMessage` 把 `base_path` 发给 worker，
  worker 读 `base_path/<writer_id>.idx`（`worker_agent.cpp:1049`）；master 也读同一 `base_path/<writer_id>.idx`
  重建 `remote_idx_`（`master_agent.cpp:1772`）。**这条路径隐含"读 idx 的进程能访问到 base_path 文件"**。

### 1.3 base_path 的共享性 —— 当前代码的真实假设

ADR-0001 §3 "Master 同一 Host 约束"原文："`worker_0.idx`（Master 数据）的数据文件可能在本地磁盘，
跨机启动时 Master 无法判断是否应加载 `worker_0.idx`，简化初始实现，避免 hostname 匹配的复杂逻辑。"

结合代码事实：
- **现状（单机 / 共享 FS 场景）**：所有进程的 `base_path` 指向同一目录（NFS / 同机），idx/meta 共享可读。
  load_db 的 `master_agent.cpp:1772` 直接读 `base_path/<writer_id>.idx` 在此场景成立。
- **多机本地磁盘场景（fly 明确支持）**：每个 worker 的 `data_path` 指向自己机器本地盘，`.dat` 不跨机共享；
  idx/meta 的 `base_path` 可能也各写各的本地路径。此时 **master 无法用 `base_path/<writer_id>.idx` 直接读**，
  load_db 的主路径在此场景**部分失效**——它依赖 `send_idx_load_to_worker` 把活派给"该 hostname 的 worker"
  去读该机器本地的 idx（`agent.py:332-340`），master 自己的 `rebuild_remote_idx_for_worker` 读 idx 那步
  在纯本地磁盘多机下会因路径不可达而 skip（`master_agent.cpp:1773-1776` 的 `exists` 检查会失败）。

> **这是 freeze 后处理方案必须回答的核心问题**：freeze 聚合产物的"权威存放位置"在哪里，
> 以及它如何被 master 在不依赖所有原 worker 机器可达的情况下访问到。

---

## 2. 现状缺口（三层，经源码核实）

### 2.1 缺口 A：idx 未 compact（轻量，已有半成品）

- `LocalIndex::compact()`（`local_index.cpp:327`）**已实现但无任何生产调用方**。
  产物：把操作日志（BEGIN/ADD/END/REMOVE）重写为无标记的干净 ADD 段外条目（原子 `.compact` + rename）。
- freeze 路径（`database.cpp:390-412`）未调 compact。
- 影响：`<writer_id>.idx` 含历史 REMOVE/BEGIN/END 噪声，体积膨胀，load 读放大。
- **本地性影响**：compact 是**纯本地操作**（改写本进程的 idx 文件），不涉及跨机，方案独立、低风险。

### 2.2 缺口 B：`removed_objects_` 的 `.dat` 物理数据未回收（中量，有 TODO）

- `database.cpp:404-411` 显式 TODO：freeze 只记日志，聚合 `.dat` 中被 `remove_object()` 删掉的对象数据仍占空间。
- `.dat` 结构（`data_writer.cpp:35`）：`data_<writer_id>_<NNN>.dat`，多对象追加，`IndexEntry.offset_/size_` 定位。
- 删除单对象需重写整个 `.dat` → 即 compaction。
- **本地性影响**：`.dat` 在写它的 worker 本地，compaction 也是**纯本地操作**（本进程重写自己的 `.dat` + 更新自己的 idx）。
  不需要跨机搬数据。影响是磁盘膨胀，与跨机无关。

### 2.3 缺口 C：跨 worker 索引聚合产物（重量，完全空白）

- architecture.md §5.3 设想的 "master 收集所有 worker idx → 合并 → 写 `merged.idx` + `_META`" **零实现**。
- `MessageType::IDX_REQUEST=15` / `IDX_RESPONSE=16`（`message_types.h:24-25`）**仅有枚举槽位，
  无 struct、无 handler、无 register**。
- **本地性影响（v2 修正的核心）**：在多机本地磁盘模型下，master **没有共享 FS 可直接读所有 worker 的 idx**。
  要聚合必须**通过网络**向各 worker 拉 idx 内容。这正是 `IDX_REQUEST/RESPONSE`（走消息体传 idx）的**真实用武之地**
  —— v1 认为"冗余"是因为错把 idx 也当成共享 FS 上的文件；v2 修正后，**网络拉取 idx 是多机本地磁盘场景下的必要路径**。

---

## 3. load_db：现有能力与局限（受本地性约束）

### 3.1 load_db 当前流程（ADR-0001）

`agent.py:276 load_db`：
1. 读 `_DB_META`（`agent.py:286-298`）拿 `db_id` + `WorkerInfo`（含 hostname）。
2. 按 hostname 分组 writer_ids（`agent.py:303-305`）。
3. 缺 worker 的 hostname → spawn 新 worker 传 `--host`（`agent.py:314-323`）。
4. 对每个 hostname，`send_idx_load_to_worker(db_id, path, writer_ids, worker_id)`（`agent.py:340`）
   → 该 host 的 worker 读 `base_path/<writer_id>.idx` 填本地 `local_idx_`（`worker_agent.cpp:1049`）。
5. master 收 `IdxLoadAck` 后，**自己重开 `base_path/<writer_id>.idx`** 重建 `remote_idx_`
   （`master_agent.cpp:1772`，`rebuild_remote_idx_for_worker`）。

### 3.2 load_db 的两种工作前提

| 场景 | 步骤 4（worker 读 idx） | 步骤 5（master 读 idx） | 结果 |
|------|-------------------------|-------------------------|------|
| **base_path 共享**（单机/NFS） | ✅ worker 读共享 FS | ✅ master 读同一共享 FS | 全索引恢复 |
| **base_path 本地磁盘多机** | ✅ worker 读自己机器本地 idx | ❌ master 读 `base_path/<wid>.idx` 因路径在远端机器而 `exists==false`，skip（`master_agent.cpp:1773`） | **worker local_idx 恢复，master remote_idx 缺失** → 调度/依赖图看不到对象 |

### 3.3 推论：freeze 后处理的真实价值

- worker 本地的 idx/数据 compaction（缺口 A/B）**与本地性无关**，任何场景都该做，是纯收益。
- 跨 worker 聚合（缺口 C）的价值**恰恰在多机本地磁盘场景**：让 master 在**不依赖各原 worker 机器持续可达**的前提下，
  持有一份聚合后的全局索引，用于依赖图可见性、调度决策、对象存在性查询。
- freeze 是天然的聚合时机：所有 worker 已 flush 完毕，idx 稳定，是拉取并聚合的全局唯一一致的快照点。

---

## 4. 分层实现方案（独立可交付）

> 三层缺口相互独立，可分别交付。A/B 纯本地；C 才涉及网络与多机。

### 4.1 方案 A：freeze 触发 idx compact（缺口 A，推荐先做，纯本地）

**目标**：freeze 时把本进程的 `<writer_id>.idx` 从操作日志格式压成干净快照。

**改动点（2 处 + 测试）**：
- `database.cpp:freeze()`（line 390-412）：在 `drain_write_back()` 之后、`on_flush()` 之前，调本 db writer 的
  `LocalIndex::compact()`。
- 测试：`src/storage/tests/database_test.cpp` 新增用例：写+删若干对象 → freeze → 断言 idx 文件无 REMOVE/BEGIN/END 标记，
  且 `LocalIndex::load()` 能读回全部存活对象。

**风险**：低。compact 用临时文件 + rename 原子替换（`local_index.cpp:328,352`）；freeze 期拒绝后续写（`check_frozen()`）。

**本地性**：✅ 纯本进程文件操作，不涉及网络、不涉及跨机。

### 4.2 方案 B：freeze 触发 `.dat` compaction（缺口 B，纯本地）

**目标**：freeze 时把本进程被 `remove_object()` 标记的对象数据从本地 `.dat` 物理删除。

**设计要点**：
- 遍历 compact 后的 idx 中存活对象的 `(file_name, offset, size)`，按 `.dat` 分组。
- 对每个本地 `data_<wid>_<NNN>.dat`：新建临时 `.dat`，按 offset 升序拷贝存活段，重建对象→新 offset 映射，
  写新 idx（指向新 `.dat`），原子 rename。删除孤儿 `.dat`。
- `IndexEntry.host_` / `write_context_hash_` 不变。

**改动点**：`database.cpp` 新增 `compact_dat_files()`，在 freeze 的 compact idx 之后调用；
需 `DataWriter` 暴露 dat 文件枚举/重建能力。

**风险**：中。重写 `.dat` 是本地 I/O 密集操作。**建议加配置开关** `freeze_compact_dat`（默认关或按
removed 占比阈值触发），见 §5-Q3。

**本地性**：✅ 纯本进程本地文件操作（`.dat` 在写它的 worker 本地）。**不需要跨机搬数据**——
每个 worker 只 compact 自己写的 `.dat`。

**关键澄清**：方案 B **不是**把数据搬到 master，而是各 worker 各自回收自己本地 `.dat` 的死空间。
聚合数据副本（跨机冗余）是现有 backup 机制（§5.4 / `master_agent.cpp:808`）的职责，与本方案正交。

### 4.3 方案 C：跨 worker 索引网络聚合 → `merged.idx`（缺口 C）

> v2 修正：此方案必须**通过网络拉取**各 worker 的 idx（多机本地磁盘下无共享 FS），
> 正是 `IDX_REQUEST/IDX_RESPONSE` 消息的真实用途。不再视为"冗余"。

#### 4.3.1 流程（freeze 后处理，master 主导）

```
freeze 全员广播完成（现有 on_database_freeze_request 已广播）
    │
    ▼ master 触发聚合（异步后台任务，不阻塞 freeze ack）
    │
    ├─ 从 _DB_META 取全部 WorkerInfo（writer_id + hostname + worker_id）
    │
    ├─ 对每个 writer，向其所属 worker 发 IdxRequestMessage(db_id, writer_id, base_path)
    │     │  （MessageType::IDX_REQUEST=15 槽位已预留，需补 struct + handler）
    │     ▼
    │  Worker 收 IdxRequest：
    │     ├─ 读本地 base_path/<writer_id>.idx（方案 A compact 后的干净格式）
    │     ├─ 序列化全部 IndexEntry 为字节流
    │     └─ 回 IdxResponseMessage(db_id, writer_id, entries_bytes)  (type=16)
    │        （若 idx 文件不存在 / 本 worker 无此 db → 回空 entries + success=false）
    │
    ├─ master 收集所有 IdxResponse（带超时 + 部分成功语义，见 §5-Q2）
    │
    ├─ 合并全部 entries 到 base_path/merged.idx
    │     格式 = 多 AddRecord 顺序排列（与 LocalIndex::compact 产物同构），
    │     每个 entry 保留原始 IndexEntry 全字段（含 host_, file_name_, offset_, size_）
    │     ── 注意：merged.idx 只聚合"索引"，不含 .dat 数据本体（见 §5-Q4）
    │
    └─ 收尾 _DB_META：追加 freeze 完成标记（不新建 _META，见 §5-Q1）
```

#### 4.3.2 merged.idx 的语义边界（必须明确）

`merged.idx` 是**全局索引视图**，不是数据副本：
- 它记录"每个对象在哪个 worker（host:port）的哪个本地 `.dat` 的 offset/size"。
- 读取该对象时，master 用 merged.idx 定位 → 仍需**网络回源**到该 worker 的 DataServer 拉 `.dat` 字节。
- 因此：**merged.idx 解决的是"索引可见性"，不解决"数据可用性"**。若某 worker 机器彻底宕机，
  其对象的索引在 merged.idx 里可见，但读取仍会失败（除非有 backup 副本，那是另一机制）。

#### 4.3.3 load_db 如何消费 merged.idx（关键改动）

`agent.py:load_db` Phase 3 增加基于 merged.idx 的 master 自恢复路径，**不依赖各原 worker 机器的本地 idx 可达**：

```
Phase 3':
if base_path/merged.idx 存在（说明 db 经历过 freeze 聚合）:
    master 直接 load merged.idx → 重建 remote_idx_（含每个对象的 host:port 定位）
    # 这步不再依赖 master 能读到各 worker 本地的 <writer_id>.idx
    # 解决了 §3.2 多机本地磁盘场景下 master remote_idx 缺失问题
    
    各 worker 的 local_idx 恢复仍走原 IdxLoadCommand 路径（读各自机器本地 idx）
    # 但若某 worker 机器不在/无对应 worker，其 local_idx 不可恢复——
    # 此时该机器对象的"读"需走 remote 路径（master remote_idx 有定位，连该 host 的 DataServer）
else:
    退化到原 IdxLoadCommand 路径（base_path 共享场景）
```

**价值**：freeze 后产出的 `merged.idx` 让后续任何 `load_db`（即使部分原 worker 机器已下线）
都能恢复 master 的全局调度视图——这是 v1 方案低估的核心价值，在多机本地磁盘模型下是真实痛点。

#### 4.3.4 消息定义（新增，补全死枚举）

`message_types.h`（枚举 15/16 已预留，补 struct）：

```cpp
struct IdxRequestMessage {
    MessageHeader header_;
    CMString db_id_;
    CMString writer_id_;       // 请求哪个 writer 的 idx
    CMString base_path_;       // worker 从此路径读 <writer_id>.idx
    static constexpr MessageType msg_type_ = MessageType::IDX_REQUEST;
    FLY_SERIALIZE(header_, db_id_, writer_id_, base_path_);
};

struct IdxResponseMessage {
    MessageHeader header_;
    CMString db_id_;
    CMString writer_id_;
    bool success_ = false;
    CMString error_message_;
    CMVector<IndexEntry> entries_;   // 直接传结构体数组（复用 IndexEntry 的 FLY_SERIALIZE）
    static constexpr MessageType msg_type_ = MessageType::IDX_RESPONSE;
    FLY_SERIALIZE(header_, db_id_, writer_id_, success_, error_message_, entries_);
};
```

**大小考量**（见 §5-Q5）：大 db 的 idx 可达百万级 entry。两种选择：
- (a) 单条 IdxResponse 装全部 entries —— 简单，但大 db 时单消息过大，需确认 MessageProtocol 的帧长度上限。
- (b) 分页/流式（IdxResponse 带 `has_more` + `page_no`）—— 复杂，但安全。

### 4.4 交付顺序（v2 调整）

| 阶段 | 内容 | 本地性 | 风险 | 前置条件 |
|------|------|--------|------|----------|
| **P0** | 方案 A：freeze idx compact | 纯本地 | 低 | 无；独立可交付 |
| **P1** | 方案 B：freeze `.dat` compaction（带开关） | 纯本地 | 中 | Q3 裁定 |
| **P2** | 方案 C：网络聚合 `merged.idx` + load_db 消费 | 跨机网络 | 中高 | Q1/Q2/Q4/Q5 裁定；新增消息类型 |

> A/B 与本地性解耦，任何部署形态都该做。C 是多机本地磁盘场景下的真实需求，
> 但也是改动最大、需最多决策的一层。

---

## 5. 开放问题（需确认）

> v1 的部分问题（Q1 `_META` 命名、Q6 backup 关系、Q7 死枚举）因方案明确化已收敛，下表为 v2 精简后的待决项。

### Q1. `_META` vs `_DB_META`：新建还是收尾？

- architecture.md §5.3 写"写 `_META`"，但 `_DB_META` 已是 writer 登记表（ADR-0001）。
- **推荐**：不新建 `_META`，freeze 完成时在 `_DB_META` 末尾追加 freeze 完成标记（或 header 加 `frozen_at`）。
  理由：单一元数据源；load_db 已读 `_DB_META`，零额外读路径。

### Q2. IdxResponse 部分成功语义（多机不可达）

- 某 worker 机器宕机 → 它的 IdxRequest 超时/失败。聚合是否：
  - (a) **尽力而为**：跳过失败 writer，merged.idx 只含可达 worker 的索引（推荐，实用）。
  - (b) **全有或全无**：任一失败则不产出 merged.idx（过严，多机常态下难满足）。
- **推荐 (a)**，并在 `_DB_META` freeze 标记里记录"聚合缺失的 writer 列表"，供 load_db 知情。

### Q3. 方案 B 的 compaction 开关 / 阈值？

- 重写 `.dat` 是本地重 I/O。
- **推荐**：`freeze_compact_dat`（默认 0=关）+ 触发阈值（如 removed 占比 > 20% 才做）。方案 A（idx compact）轻量，默认开。

### Q4. merged.idx 的数据可用性边界（需对用户文档化）

- 重申 §4.3.2：**merged.idx 只恢复索引，不恢复数据本体**。某 worker 机器彻底不可达时，其对象索引可见但读取仍失败。
- 是否接受此边界？若要"机器宕机也能读"，需 freeze 时连带聚合 `.dat` 数据字节到 master（=数据副本，
  量级远大于索引，与 backup 机制重叠）。**推荐：不在此方案做数据聚合，保持与 backup 正交。**

### Q5. IdxResponse 大 db 的分页策略

- 单条消息装百万级 entry 可能超帧上限。
- **需确认**：MessageProtocol 的帧长度上限是多少？是否已有大 payload 的处理范式（参考两段式 DataResponse）？
- 若上限足够大或可配置 → 选 §4.3.4 (a) 单条；否则选 (b) 分页。**实现前必须核实此项**。

### Q6. 聚合触发时机：在线（freeze 同步）还是离线（独立命令）？

- **在线**：freeze handler 广播后自动触发聚合（异步后台任务，不阻塞 freeze ack）。语义简单（freeze 完成≈聚合完成）。
- **离线**：提供 `fly.merge_db(path)` 或 CLI，用户显式触发。
- **推荐**：在线异步（后台任务），freeze ack 只表示"全员已 frozen"，聚合完成后写 merged.idx + _DB_META 标记。
  load_db 以"merged.idx 存在 + _DB_META freeze 标记"为聚合完成的判定。

### Q7. 多机 base_path 不一致问题（v2 新增，最深的问题）

- 多机本地磁盘场景下，每个 worker 的 `base_path` 可能是各自机器的本地路径（如 `/data/mydb`），
  字符串相同但物理位置不同。`_DB_META` 里只记 `hostname`，不记每个 writer 的 base_path。
- 这导致：(a) IdxRequest 传给 worker 的 `base_path` 该填什么？(b) merged.idx 里的 IndexEntry 如何标注
  "这个对象在哪个机器"？当前 `IndexEntry.host_` 字段（`index_entry.h:15`）记录写入时 host，可用于此。
- **需确认**：多机部署时，`open_db(path, data_path)` 的 path 是用户传入的"逻辑路径"（各机器相同字符串）
  还是"每机器不同物理路径"？这决定 base_path 是否需要随 WorkerInfo 持久化。

---

## 6. 受影响文件预估（按阶段）

### 方案 A（纯本地）
- `src/storage/cpp/database.cpp` — freeze() 调 compact
- `src/storage/tests/database_test.cpp` — 新增 freeze+compact 用例

### 方案 B（纯本地）
- `src/storage/cpp/database.{h,cpp}` — `compact_dat_files()`
- `src/storage/cpp/data_writer.{h,cpp}` — dat 枚举/重建能力
- `src/core/cpp/config.cpp` — `freeze_compact_dat` 开关
- 测试：`database_test.cpp` / 新 `compaction_test.cpp`

### 方案 C（跨机网络 + 新消息）
- `src/network/cpp/message_types.h` — 补 `IdxRequestMessage` / `IdxResponseMessage`（枚举 15/16 已在）
- `src/network/tests/message_protocol_test.cpp` — round-trip 测试
- `src/agent/cpp/master_agent.{h,cpp}` — 聚合主控：发 IdxRequest、收 IdxResponse、写 merged.idx、_DB_META 收尾
- `src/agent/cpp/worker_agent.{h,cpp}` — IdxRequest handler：读本地 idx 回 entries
- `src/agent/py/agent.py:load_db` — Phase 3' merged.idx 消费路径
- `src/storage/cpp/local_index.{h,cpp}` — 复用 load/encode，可能加 `load_merged()` / `write_merged()`
- `src/storage/cpp/db_meta.{h,cpp}` — freeze 完成标记
- 测试：`master_agent_test.cpp`（聚合 + 部分失败）+ agent 侧 load_db 消费 merged.idx 用例

---

## 7. 与现有文档的对齐（实现后需同步修订）

| 文档 | 修订点 |
|------|--------|
| `docs/architecture.md` §5.3 | 标注 freeze 后处理已实现哪些层（A/B/C）；澄清 `_META` 实为 `_DB_META` 收尾 |
| `docs/architecture.md` §6.3 消息表 | `IDX_REQUEST/RESPONSE` 从"reserved"改为"已实现"，填字段 |
| `docs/architecture.md` §1.x 数据本地性 | 补充"多机本地磁盘"模型的明确描述（当前文档对此含糊） |
| `docs/roadmap.md` §五降级区 F2 | 按实际交付更新状态 |
| `docs/adr/` | 新增 ADR：记录"为何 freeze 聚合走网络 IdxRequest 而非共享 FS"（多机本地磁盘前提） |
| `docs/adr/0001-db-meta-and-load-db.md` §3 | "Master 同一 Host 约束" 若被方案 C 解除，需更新 |
| `CLAUDE.md` / `docs/storage/module.md` | freeze 流程 + DataServer/idx 本地性描述更新 |

---

## 8. v1 → v2 变更说明

| 项 | v1（错误前提） | v2（修正前提） |
|----|----------------|----------------|
| 数据本地性 | 假设 master/worker 共享 FS，直读 idx | 多机本地磁盘：`.dat` 在各 worker 本地，读走 DataServer TCP；idx 的 base_path 共享性是场景相关 |
| IdxRequest/Response | 判定为"冗余，不实现" | 判定为"多机本地磁盘场景下必要"，纳入方案 C |
| merged.idx 价值 | "仅 worker 不可达时的索引 fallback" | "让 master 在不依赖各原 worker 机器本地 idx 可达时，持有全局调度视图"——核心价值 |
| 方案 C 路径 | master 本地聚合（读共享 FS） | master 主导 + 网络拉取各 worker idx（IdqRequest/Response） |
| 开放问题 | 7 项 | 精简 + 新增 Q7（多机 base_path 一致性，最深问题） |

---

*文档制定日期：2026-07-22（v2）*
*基于 commit `e1aac14` 的源码核实 + 用户对数据本地性前提的纠正*
