# Fly 长时运行内存增长分析

> 场景：单次 run 运行十几到几十小时，积累大量数据对象的 meta 信息。
> 分析日期：2026-06-30（基于源码逐项核实）
> 核心问题：**当前实现会造成内存快速上升吗？**

---

## 一、结论速览

**会持续增长，但不是"快速"上升，也不是泄漏——而是"无上限累积"。**

真正的内存增长来自**数据对象元信息的索引**（`local_idx` / `remote_idx`），而非 task 元数据。task 侧已有完善的上限与清理；对象索引侧**只在显式 `remove_object` 时清理**，只要数据对象不删除，索引就只增不减。

在"几十小时积累 N 万个数据对象"的场景下，master 内存主要随**对象数量**线性增长，与 run 时长无直接关系——而与**产出了多少不删除的对象**成正比。

---

## 二、内存项分类（逐项源码核实）

### ✅ A. 有硬上限 / 完善清理 —— 不增长

| 内存项 | 位置 | 上限 / 清理机制 |
|---|---|---|
| **TaskManager**（task 元数据） | `task_manager.h:19` | `kMaxCompletedTasks = 100`，超限按 `completed_at` 淘汰最旧（`maybe_cleanup_completed`） |
| **task_modules_/task_args_/task_vars_** | `master_agent.h:155-158` | task 完成 `on_task_complete` / `on_task_failed` 时 `erase`（master_agent.cpp:493/547） |
| **task_dependency_locations_** | `master_agent.h:168` | `assign_task_to_worker` 时 consume 后 `erase`（master_agent.cpp:634） |
| **DependencyGraph** | task 完成 `remove_task` | `on_task_complete` / `on_task_failed` 调 `graph_->remove_task`（master_agent.cpp:846/883） |
| **ObjectCache**（读缓存） | `object_cache.h` | `read_cache_size` 默认 1GB，LFU 淘汰 + 1.5× 硬限制 + 30s 保护期 |

> task 侧的清理是完善的——这是调度状态，SeaScape 式"调度器保持 <2GB 轻量"
> 原则（competitor-analysis §5.2）在 task 维度已满足。

### ⚠️ B. 只在显式 remove 时清理 —— 随对象数线性增长（核心增长源）

| 内存项 | 位置 | 单条占用 | 清理条件 |
|---|---|---|---|
| **DataService::local_idx_** | `data_service.h:288` | 见下估算 | 仅 `remove_local_index()`（用户调 `db.remove_object()`） |
| **DataService::remote_idx_** | `data_service.h:292` | ~100 B/对象 | 仅 `remove_remote_index()` |
| **write_provenance_** | `master_agent.h:230` | ~64 B/对象（两个字符串） | 对象删除时 erase（master_agent.cpp:916/1271） |
| **recorded_workers_** | `master_agent.h:227` | 三元组 | 从不清理（每个 db+writer 一次，量小） |

**关键性质**：这几项**没有数量上限、没有 TTL、没有 LRU**。只要数据对象存在，元信息就常驻内存。

---

## 三、单对象内存估算（local_idx 的隐藏开销）

`local_idx` 是 master/worker 各自进程内的对象索引。每个数据对象对应一个 `LocalObjectInfo`（`data_service.h:63-74`）：

```cpp
struct LocalObjectInfo {
    CMString db_id_;                          // ~24 B（短）
    CMVector<IndexEntry> entries_;            // 每个 IndexEntry ~120 B（见下）
    bool flushed_ = false;
    CompletionState completion_state_;
    CMString error_message_;
    bool is_temp_;
    FlyBufferPtr temp_compressed_data_;       // shared_ptr（temp 对象才持有数据）
    std::mutex cv_mutex_;                     // ← 重量级：libc++ 下 ~40-56 B
    std::condition_variable cv_;              // ← 重量级：libc++ 下 ~48 B
};
```

每个 `IndexEntry`（`index_entry.h:7-16`）：

```cpp
struct IndexEntry {
    CMString object_name_;     // full name，~40 B
    CMString file_name_;       // ~24 B
    int64_t offset_;
    int64_t size_;
    bool is_large_;
    int32_t block_count_;
    CMString host_;            // ~16 B
    CMString write_context_hash_;  // ~32 B（SHA 哈希）
    // 小计 ~140 B（字符串按 SSO 估算，长名更长）
};
```

### 估算：一个普通对象 ≈ 250-400 B

- `LocalObjectInfo` 基础 ~120 B（含 mutex+cv 的 ~90-100 B 固定开销）
- 1 个 `IndexEntry` ~140 B
- **每个对象约 250-400 B**

### 量级换算

| 对象数 | local_idx 内存（单进程） |
|---|---|
| 1 万 | ~3-4 MB |
| 10 万 | ~30-40 MB |
| 100 万 | ~300-400 MB |
| 1000 万 | ~3-4 GB ⚠️ |

> 注：mutex + condition_variable 是**每个对象一份**（用于写完成等待）。
> 这是隐藏的高开销——100 万对象光 mutex+cv 就占 ~100 MB。

---

## 四、Master vs Worker 的增长差异

### Master 增长（最敏感）

master 同时持有：
- `remote_idx_`（**所有**对象的 placement + size + workers + 访问计数）—— 全局视图
- `local_idx_`（master 自己写的对象）
- `write_provenance_`（**所有**对象的 write_context_hash）

**master 的 remote_idx_ 是全局的**：每个 worker 写的每个对象，master 都在 remote_idx_ 登记一条。
因此 master 内存 ≈ **集群所有对象总数 × ~100 B**。

10 万对象 → master ~10 MB（remote_idx）+ write_provenance ~6 MB ≈ **可控**。
100 万对象 → master ~100 MB+ —— **开始需要关注**。

### Worker 增长

每个 worker 的 `local_idx_` 只含**自己写的**对象（mutex+cv 开销在这里）。
worker 内存 ≈ **本 worker 写的对象数 × ~300 B**。

---

## 五、与 SeaScape 定位的对照

competitor-analysis §5.2 P0 强调"调度器保持 <2GB 轻量"。当前实现在 task 维度满足（kMaxCompletedTasks=100），但在**数据对象维度**没有上限：

- SeaScape 的纪律是"调度状态进 db，Master 内存 <2GB"。
- fly 当前把**所有对象元信息**常驻 master 内存（remote_idx_ + write_provenance_）。
- 100 万对象量级时，master 仅 remote_idx+provenance 就可能逼近 GB 级。

**这与 roadmap §三 A1（locality 解耦）是同类问题**：调度相关的内存状态应能落盘/换出，而非无限常驻。

---

## 六、是否需要现在处理？

### 判断矩阵

| 场景 | 预期对象量 | master 内存 | 处置 |
|---|---|---|---|
| 典型 EDA 流程 | 1-10 万 | <20 MB | ✅ 无需处理 |
| 大型仿真 / 多 db | 10-100 万 | 20-200 MB | 🟡 监控，可接受 |
| 极端长 run + 海量小对象 | >100 万 | >GB | 🔴 需要上限机制 |

### 当前阶段建议

**短期（本阶段不阻塞）**：单次 run 十几小时、对象量在十万级以内，内存增长在几十 MB 量级，**可接受，不需要立即处理**。

**中期（写入 roadmap）**：当出现"对象量 >100 万"或"master 内存 >500MB"的真实痛点时，补一个**对象元信息的 LRU/TTL 上限**：
- 给 `remote_idx_` 加 max_entries 配置项，超限时淘汰访问最久的对象元信息（数据仍在磁盘，仅内存索引换出）
- `write_provenance_` 同步淘汰
- 这与 ObjectCache 的 LFU 模式同构，可复用经验

**不做**：不要给 `local_idx_`（worker 自写对象索引）加淘汰——它是对磁盘 idx 文件的内存镜像，淘汰会导致写入/读取语义复杂化。worker 的 local_idx 增长由"本 worker 写了多少对象"决定，通常远小于全局量。

---

## 七、风险点清单（供后续处理参考）

| # | 风险 | 严重度 | 触发条件 |
|---|---|---|---|
| R1 | `remote_idx_` 无上限累积 | 中 | 全局对象 >100 万 |
| R2 | `write_provenance_` 无上限累积 | 低-中 | 同上（单条更小） |
| R3 | `LocalObjectInfo` 每对象含 mutex+cv | 低 | 单 worker 写 >100 万对象 |
| R4 | freeze 后 idx 永久不可清理 | 低 | freeze 的 db 对象永不删（但通常 freeze 是终态，量固定） |
| R5 | `recorded_workers_` 从不清理 | 极低 | 每个(db,writer) 一次，量极小 |

> R1/R2 是唯一有实际意义的增长项，且仅在百万级对象时才凸显。
> task 侧（含 DependencyGraph / TaskManager / task_args）已完善，无风险。

---

## 八、一句话回答

> **当前实现不会造成"快速"内存上升——task 元数据有 100 条硬上限、读缓存有 1GB LRU、task 临时状态在完成时即清理。真正的持续增长来自"数据对象的元信息索引"（remote_idx/local_idx/write_provenance），它随对象数量线性增长、无上限，但只在百万级对象（master 内存数百 MB）时才需要关注。对于十几小时、十万级对象的典型 run，内存增长在几十 MB 量级，完全可接受——不需要现在处理，但应记入 roadmap 作为"对象量超百万"时的待办项。**

---

*分析日期：2026-06-30*
