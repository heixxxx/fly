# Fly 增强路线图

> 基线：`competitor-analysis.md`（2026-06-25）+ 2026-06-30 源码核实
> 制定日期：2026-06-30
> 决策依据：用户对 P0/P1/P2 各项的明确裁定（见 §4 决策记录）

---

## 一、状态基线：竞品分析已部分过时

`competitor-analysis.md` §1.3 列了 6 项"尚未实现"，但该文档写于 2026-06-25，其后的提交 `b4f8259`/`da51fb0`/`ef32061`/`0bb00f1`/`79e92a9` 已实质落地多项。经源码重新核实：

| 竞品分析声称"未实现" | 2026-06-30 真实状态 | 证据 |
|---|---|---|
| Locality 调度是空函数 | ✅ **已实现且默认开启** | `task_scheduler.cpp:114 compute_scores()`；`config.cpp:109 locality_scheduling_enabled=1` |
| 远程读退避未做 | ✅ **已实现** | `data_service.cpp:978` TIER2/TIER3 多副本容错 + 指数退避；`net_quality_monitor.cpp` 网络感知排序 |
| Worker 失败恢复 | 🟡 部分（task 级重跑 + 防死锁） | `master_agent.cpp:920 on_disconnect` 重新入队；`rollback_pending_frozen` 防永久冻结 |

**结论**：竞品分析需更新 §1.3。真正仍未实现的项缩减为下文 §2 所列。

---

## 二、当前真实短板清单（经源码核实）

### 2.1 架构债（review 已记录未解）

| 编号 | 短板 | 证据 |
|---|---|---|
| ~~**A1**~~ | ~~locality 实现违反六层分层：`TaskScheduler`（Layer 3）向下依赖 `DataService`（Layer 1）~~ | **✅ 已完成**（commit 1b2ad12：master 预计算 locality hint 注入 graph，scheduler 只消费 POD，解除 task→storage 依赖） |

> §3 的方案已按此执行完毕，保留作为决策记录。

### 2.2 功能空缺

| 编号 | 短板 | 证据 | 处置 |
|---|---|---|---|
| **F1** | SSH / 多机 Worker 启动 | `launch_ssh_workers` 全仓库零命中；仅 `subprocess.Popen` 本机 | **功能已具备，降级**：见 §4 决策记录 ① |
| **F2** | Freeze 后处理（idx 合并 / merged.idx / _META 聚合） | master 无 `IdxRequest` handler；`grep merged.idx src/` 零命中 | **降级**：见 §4 决策记录 ②（仍未实现） |
| **F3** | Worker role（storage_only / hybrid） | 已落地（2026-08-15）：静态身份 + idle 候选层过滤，scheduler 零 role 概念 | 完成 |
| **F4** | 大对象分片传输 + 背压 | DataResponse 两段式但不分片；仅连接池并发限流，无 credit 流控 | **降级**：见 §4 决策记录 ⑤ |
| **F5** | ~~任务优先级~~ | ~~`TaskRequirements` 无 priority 字段；纯 FIFO~~ | **✅ 已完成**（commit 500880c：`@as_task(priority=N)` 全链路优先级调度，ready_tasks_ 按 {-priority, task_id} 有序） |
| **F6** | stage checkpoint 显式表达 | 无框架级 progress query | **不做**：见 §4 决策记录 ④ |
| **F7** | 协议版本号 | `MessageHeader` 无 version 字段 | **不做**：见 §4 决策记录 ⑥ |

---

## 三、Locality 分层修复方案（核心交付）

### 3.1 问题定位

**违反点**（两处，必须同时解除）：

1. `src/task/cpp/BUILD:16` — `fly_task` 的 `deps` 含 `//src/storage/cpp:fly_storage`
2. `src/task/cpp/task_scheduler.h:4` — `#include <storage/cpp/data_service.h>`

**根因**：scheduler 在 `compute_scores()` / `select_best_worker()` 里直接调 `DataService::instance()->get_remote_workers()/get_remote_size()` 现算 locality 分数，而非消费上游预计算的 POD hint。

**为何是问题**：
- 违反 `architecture.md §4.1` 宣称的"六层清晰架构 + BUILD 级无循环依赖"核心优势（task=Layer 3 向下依赖 storage=Layer 1）。
- 破坏测试可隔离性：单测 `task_scheduler_test.cpp:594` 注释自称"用 fake DependencyGraph 不依赖真实 DataService"，实际 T1–T5 每个都调真实 `DataService::instance()->update_remote_idx()`，靠手动清理维持隔离，脆弱。

**性能层面**：review §2 的 O(W²) 指控**当前已不成立** —— `compute_scores`（`task_scheduler.cpp:141`）已改用 worker_id 直接索引 `score_buf_[h]`，复杂度 O(deps×holders)。故本次修复**只针对分层依赖，不涉及性能**。

### 3.2 方案选型：回归工作流 C（预计算 hint）

经评估采用 **方案 A（回归设计）**，理由：

| 维度 | 方案 A：预计算 hint | 方案 B：修订设计接受依赖 |
|---|---|---|
| 分层纯洁性 | ✅ 恢复 BUILD 无环 | ❌ 永久打破 Layer 3→1 隔离 |
| 测试隔离 | ✅ scheduler 可纯单测（fake graph） | ❌ 永久依赖真实 DataService singleton |
| 后续扩展 | ✅ hint 是 POD，易序列化/跨进程（为多机铺路） | ❌ 每加一个查询维度就加深耦合 |
| 改动量 | 中（见 §3.3，4 处改动） | 小（改文档+注释）但留债 |

**决策：选方案 A。** 分层无环是 fly 对外宣称的核心工程优势（见竞品分析 §4.2），不应为省一次重构而永久放弃。且 hint 是 POD 数据，天然适配未来多机调度（master 预计算后随 task 派发），方案 B 会让多机化时耦合更深。

### 3.3 实现步骤（4 处改动 + 测试）

**改动 1 — `TaskRequirements` 增加 locality hint 字段**（`src/task/cpp/dependency_graph.h:17`）

```cpp
struct TaskRequirements {
    CMVector<CMString> capabilities_;
    float timeout_seconds_ = -1.0f;

    // Locality hint：master 预计算的 worker→亲和分映射（worker 持有的输入字节数）。
    // scheduler 只消费此 POD，不接触 DataService。空 = 无 locality 信息（退化 FIFO）。
    CMVector<std::pair<uint64_t, int64_t>> locality_order_{};
};
```

**改动 2 — `DependencyGraph` 增加 hint setter**（`src/task/cpp/dependency_graph.h`）

```cpp
// master 在 schedule 前，按 task 的依赖对象查 DataService 预计算 locality_order_，
// 写入此结构。scheduler 只读不查。
void set_task_locality_hint(uint64_t task_id,
                            CMVector<std::pair<uint64_t, int64_t>> hint);
```

**改动 3 — `TaskScheduler` 改为只消费 hint**（`src/task/cpp/task_scheduler.cpp`）

- `compute_scores()` 改为从 `reqs.locality_order_` 填 `score_buf_`，**删除** `DataService::instance()` 调用（`task_scheduler.cpp:135`）。
- `select_best_worker()` 阶段 B 逻辑不变（仍按 score 降序选 idle worker）。

**改动 4 — 解除分层依赖**

- `src/task/cpp/task_scheduler.h:4` 删除 `#include <storage/cpp/data_service.h>`
- `src/task/cpp/BUILD:16` 从 `deps` 移除 `//src/storage/cpp:fly_storage`
- `src/task/cpp/BUILD:26` 从 `dynamic_deps` 移除 `fly_storage_so`

**改动 5 — master 侧预计算注入**（`src/agent/cpp/master_agent.cpp:403 schedule_tasks()`）

在 `scheduler_->schedule_all_available()` 之前，对每个 ready task 预计算 hint：

```cpp
// master（Layer 4）合法持有 DataService，预计算 locality hint 注入 graph（Layer 3）。
// scheduler 只消费 POD，分层无环。
if (Config::instance()->get_int("locality_scheduling_enabled") == 1) {
    auto ready = graph_->get_ready_tasks();
    auto ds = DataService::instance();
    for (uint64_t tid : ready) {
        auto deps = graph_->get_task_dependencies(tid);
        CMUnorderedMap<uint64_t, int64_t> acc;  // worker_id → 累计字节数
        for (const auto& obj : deps) {
            int64_t sz = ds->get_remote_size(obj);
            for (uint64_t h : ds->get_remote_workers(obj)) {
                acc[h] += sz;
            }
        }
        CMVector<std::pair<uint64_t,int64_t>> hint(acc.begin(), acc.end());
        graph_->set_task_locality_hint(tid, std::move(hint));
    }
}
scheduler_->set_locality_preference(...);
```

**hint 失效时机**：remote_idx 在 master 侧的更新点（`master_agent.cpp` 行 813/844/890/1210/1245/1265/1391/1721/1775）都会触发 `schedule_tasks()`，而预计算在 `schedule_tasks()` 入口做，故 hint 总是最新——**无需额外失效逻辑**。

**测试改造**（`src/task/tests/task_scheduler_test.cpp`）

- 移除所有 `DataService::instance()->update_remote_idx(...)` 调用（行 608/629/652/675/701/742）。
- 改为 `graph_->set_task_locality_hint(task_id, {{2, 100}})` 直接注入 POD hint。
- 移除 `remove_remote_index` 清理（行 617/638/...），单测天然隔离。
- 修正行 594-596 注释使其与实现一致。

### 3.4 验收标准

1. `./fly.sh build //src/task/...` 通过，且 `task_scheduler.h` 不再 include storage。
2. `grep "storage" src/task/cpp/BUILD` 零命中。
3. `task_scheduler_test.cpp` 中 `grep "DataService"` 零命中。
4. 全量 QA 通过（含 `qa/scheduling/test_locality_*.py` 三层测试）。
5. ~~同步修订 locality 三部曲文档~~（2026-08-16 文档重组已删——任务完结，决策记录在本 roadmap §三与 DOC_CHANGELOG）。

### 3.5 风险与缓解

| 风险 | 缓解 |
|---|---|
| 预计算增加 `schedule_tasks()` 开销（遍历 ready task 查 DataService） | ready task 数通常远小于 pending；且原实现每次 `select_best_worker` 都查，新实现每 task 只查一次，**总查询次数下降** |
| hint 数据结构 `CMVector<pair>` 序列化未覆盖 | 当前 hint 是进程内临场计算，不需序列化；若未来多机需跨进程派发，再补 `FLY_SERIALIZE` |
| 改动触碰调度热路径 | TDD：先改测试（注入 hint 验证三阶段算法不变量 T1–T6 仍守护），再改实现 |

---

## 四、决策记录（用户裁定，2026-06-30）

| # | 原提案 | 用户裁定 | 理由 |
|---|---|---|---|
| ① | P0：SSH 多机部署 | **功能已具备，仅缺测试环境** | SSH 功能层面已有；开发环境单一 WSL 难以验证多机。降为"待测试环境就绪"，非代码工作 |
| ② | P0：Freeze 后处理 | **降级** | 当前 `load_db` 在 worker 齐备时能正确加载全部索引，freeze 聚合非阻塞需求。移出 P0 |
| ③ | P0：修复 locality 分层违反 | **保持，且为首要交付** | 用户确认实现时确实遇困难，要求给出可行方案（见 §3） |
| ④ | P1：stage checkpoint | **不做** | db 级 checkpoint 足够，无需额外的阶段进度表达 |
| ⑤ | P1：大对象分片+背压 | **降级** | 当前架构基本满足要求，非紧迫 |
| ⑥ | P2：协议版本号等 | **不做** | 早期开发阶段没必要引入版本号 |
| ⑦ | P1：任务优先级（F5） | **保持** | FIFO 不足场景真实 |
| ⑧ | P1：Worker role 调度（F3） | **完成** | 已落地（静态身份 + 调度候选层过滤） |

---

## 五、执行优先级（最终版）

### 🔴 P0 — 本阶段唯一硬交付

**[A1] Locality 分层修复**（§3 详述）— ✅ **已完成（2026-06-30）**
- 解除 task→storage 的 BUILD/include 依赖
- 改为 master 预计算 hint + scheduler 消费 POD
- 测试改为 hint 注入，恢复可隔离性
- 配套修订 plan/review 文档
- 验证：全量单测 50/50 + 全量 QA 111/111 通过；`bazel query deps(//src/task/cpp:fly_task)` 依赖闭包零 storage

### 🟡 P1 — 本阶段可选，视精力推进

**[F5] 任务优先级** — ✅ **已完成（2026-07-31）**
- `TaskRequirements` 加 `int priority_ = 10`（默认中点值，可双向调节：<10 让路，>10 抢先）
- `get_ready_tasks()` 按 `(priority desc, task_id asc)` 排序；scheduler first-fit 天然实现 head-of-line skip（高优先级缺 worker 不阻塞低优先级）
- 全链路透传：TaskMetadata（worker 崩溃恢复）+ TaskSubmitMessage（worker→master 递归提交）+ Python `@as_task(priority=N)` 独立关键字
- 完全向后兼容：所有现有 task 默认 10（同值），排序退化为 task_id 升序 = 现状 FIFO
- 验证：全量单测 52/52 + 全量 QA 135/135 通过；详见 [`docs/priority-scheduling-design.md`](priority-scheduling-design.md)

**[F3] Worker role（已落地 2026-08-15，实现与原方案不同）**
- `_spawn_process_worker` 消费 role 字段 → CLI `--worker-role` → 注册上报（静态身份，
  不可变更；重连同值）
- 调度决策不感知 storage_only：`WorkerManager::get_idle_workers` 候选层过滤
 （scheduler/TaskRequirements 零 role 概念——比原方案"scheduler 规则+隐式
  requires"更彻底，调度器完全不感知）
- storage_only 仍参与：心跳判死、数据面、internal 数据 task（merge/backup）、
  backup 目标选择

**[M1] 数据对象元信息的内存上限（观察项）** — 详见 [`memory-growth-analysis.md`](memory-growth-analysis.md)
- **现状**：`remote_idx_` / `local_idx_` / `write_provenance_` 只在显式 `remove_object` 时清理，无数量上限/TTL/LRU，随对象数线性增长。task 侧已有完善上限（`kMaxCompletedTasks=100` + 完成即清理），数据对象侧没有。
- **触发阈值**：全局对象 >100 万、或 master 内存 >500MB 成为真实痛点时启动
- **方案方向**：给 `remote_idx_` 加 `max_entries` 配置 + LRU 淘汰（按 `last_access_time_`，数据仍在磁盘仅内存索引换出）；`write_provenance_` 同步淘汰。与 ObjectCache 的 LFU 模式同构，可复用经验
- **不做**：不给 `local_idx_`（worker 自写对象索引）加淘汰——它是磁盘 idx 的内存镜像，淘汰会破坏写/读语义
- **当前结论**：十几小时 / 十万级对象的典型 run，内存增长在几十 MB 量级，**可接受，不阻塞**

**[S1] 存储层内存/磁盘优化清单**（2026-08-01 db_id 废弃调研发现）

> 来源：调研"idx 文件名存储方式与 db_id 冗余"时逐项核实源码得出。S1-1 在本批 db_id 废弃改造中修复，S1-2/3/4 作为后续优化方向记录。

| 编号 | 问题 | 位置 | 量级 | 状态 |
|------|------|------|------|------|
| **S1-1** | 磁盘 idx 文件每条记录的 `object_name_` 冗余存 db_id 前缀（`db_id:short`）— 同一 .idx 文件天然属于同一 db，前缀 100% 冗余 | `LocalIndex::entries_` key + `IndexEntry.object_name_`（`local_index.h:69`、`index_entry.h:8`） | 每条 11 字节 × N；百万对象 ≈ 10 MB 磁盘 | ✅ **已修复**（db_id 废弃改造：LocalIndex 改存 short_name） |
| **S1-2** | `LocalObjectInfo` 每对象含独立 `std::mutex` + `std::condition_variable`（用于写完成等待） | `data_service.h:66-67` | libc++ 下 ~88 B/对象固定开销（mutex 40B + cv 48B）；百万对象 ≈ 88 MB | ✅ **已修复**（2026-08-15 前落地）：per-object mutex/cv 死路径删除，`completion_state_` 改 `std::atomic`（根治锁外裸读竞争），等待改 per-db 共享 cv + predicate（见下方说明） |
| **S1-3** | `remote_idx_` / `write_provenance_` 无上限累积（= M1 的 R1/R2） | `data_service.h:312-314`、`master_agent.h:349` | master 随全局对象数线性增长；百万对象 ≈ 100 MB+ | 🟡 = M1，待对象量真实过百万时启动（见下，2026-08-02 调研补充） |
| **S1-4** | master `recorded_workers_` 从不清理（= M1 的 R5） | `master_agent.h` | 每个 (db,writer) 一条，量极小 | ⚪ 极低优先级 |

**S1-2 的优化方向**（`LocalObjectInfo` 的 mutex+cv 开销）—— ✅ **已按推荐方案完成**：删除 `cv_mutex_`/`cv_` 字段 + `_or_wait` 死路径；`completion_state_` 改 `std::atomic<CompletionState>` 根治锁外裸读数据竞争；本地写完成等待改为 per-db 共享 cv + predicate 检查目标对象 completion_state_（`data_service.h:83`）。以下调研记录保留作决策背景。

当前每个未完成/等待中的写对象各持一份 `mutex`+`cv`，用于读路径 `try_read_local_raw_or_wait` 阻塞等待写完成。这是单对象百字节级开销的主项（远大于 S1-1 的 11 字节前缀）。百万对象时仅 mutex+cv 就占 ~88 MB。

**2026-08-02 调研补充 — 实为死代码**：全树 grep 确认 `try_read_local_or_wait` / `try_read_local_raw_or_wait`（`data_service.cpp:811/959`）**无任何生产调用方**，仅被单元测试引用（`src/storage/tests/data_service_test.cpp`、`write_registration_test.cpp`）。生产读路径 `read_raw_compressed`（`data_service.cpp:1064`）走 3-tier + `can_still_produce` 语义（见 S4），**从不依赖 per-object cv**。写完成路径的 5 处 `notify_all()`（`data_service.cpp:276/309/328/403/1373`）在生产中唤醒空。

**额外隐患（顺带可根治）**：`completion_state_` 是普通 enum，在锁外被裸读（`data_service.cpp:828/842/871/878-879/890...`），存在数据竞争。当前靠"空 `cv_lock` 块"（`{std::lock_guard<std::mutex> cv_lock(info->cv_mutex_);}` 立即析构，`:307/401/1371`）建立 release/acquire 屏障 —— 脆弱补丁。

可行方向（待启动）：
- **直接删除死路径**（推荐）：删除 `cv_mutex_`/`cv_` 字段 + 两个 `_or_wait` 函数 + 5 处 `notify_all()` + 3 处空屏障块；`completion_state_` 改 `std::atomic<CompletionState>` 顺带根治数据竞争。生产零影响。
- 共享等待（仅当未来确需同步等待 API）：把 per-object 的 mutex+cv 改为 per-db 或全局的等待机制（如 `std::condition_variable_any` + 一个集中的 `unordered_set<写中对象>`），用一次 hash 查找替代每对象一份同步原语
- 代价：等待唤醒粒度变粗（notify 时需 broadcast 或按 key 路由），实现复杂度上升
- 触发阈值：单 worker 写 >100 万对象、或 `local_idx_` 内存成为瓶颈时

**S1-3 的优化方向**（2026-08-02 调研补充）：`remote_idx_` 与 `write_provenance_` 分属不同类、淘汰策略差异大，需分开处理。

`remote_idx_` 淘汰策略（master vs worker）：
- **worker 侧淘汰完全安全** —— miss 走 TIER3 回查 master（`worker_agent.cpp:747 request_remote_data`）刷新本地 `remote_idx_` 重填，重入 TIER2，仅多一次 master RPC（已有降级路径）。
- **master 侧是 location authority，淘汰需谨慎** —— master 淘汰后 worker 查不到会误判对象不存在；但对象若真存在，`.idx` 在共享 FS 上，可经 `rebuild_remote_idx_for_worker`（`master_agent.cpp:1821`，读 `.idx` 重建）恢复。建议**优先淘汰 worker 侧，master 侧保守或配套"miss 时重建"**。

`write_provenance_` 发现 **merge 漏清**（详见 S3）：`cleanup_after_merge`（`master_agent.cpp:2406-2412`）调 `clear_remote_index_for_db` / `clear_local_index_for_db` 清索引，**完全没碰 `write_provenance_`**。跨 path merge 后源 db_path 写 `_MIGRATED_TO` 重定向，源命名空间旧 provenance 条目成为孤立条目。

已有可复用基础设施：
- `RemoteObjectMeta`（`data_service.h`）已含 `read_count_` / `last_access_time_` / `size_bytes_`，与 `ObjectCache::evict()`（`object_cache.h:255-284`）的 score（`read_count/age`）+ 30s 保护窗口 + 1.5x 硬上限模式完全对应，可直接套用。
- ~~`decay_remote_access`（访问计数衰减，未接线）+ `backup_decay_interval` / `backup_decay_factor` 配置~~——**2026-08-16 已删除**：auto_backup 双层重设计（worker suggest + master EWMA）落地后全链死代码，随死代码清理批次移除。
- 建议新增配置：`{"remote_idx_max_entries", 0}`（0=unlimited 默认兼容，>0 触发淘汰），仿 `read_cache_size` / `temp_store_size`（`config.cpp`）格式。

**当前结论**：S1-1 已在 db_id 废弃改造中修复；S1-2/S1-3 在十万级对象量级下内存可控（数十 MB），**不阻塞，待真实痛点触发**。与 M1 同属"对象量过百万"的待办集合。

---

**[S3] `write_provenance_` 健壮性不足**（2026-08-02 调研发现）— ✅ **已修复**（2026-08-12 push，commit 1bdf244）：嵌套 map 重构 + 时间戳填空 hash（裸写入）+ load 重建（持久化）+ freeze 清理 + merge 不继承/清理孤立条目 + master remove bug 修复 + 8 个 TDD 测试。以下调研记录保留作背景。

`write_provenance_`（`master_agent.h:349`，仅 master 进程，`unordered_map<object_name, write_context_hash>`）守护核心不变量：**同一对象名只能被同一 write context 写出**，防止不同任务逻辑向同一对象名写入不同内容（破坏 fly 的确定性 / 可重现性保证）。校验在 `do_write_register`（`master_agent.cpp:1256-1259`）：对象不存在则登记 hash；存在且 hash 相同则允许（幂等重算）；存在但 hash 不同则拒绝（`WRITE_PROVENANCE_MISMATCH`）。该机制还支撑 backup task（`master_agent.cpp:2060`，备份任务继承源 provenance）和 merge task（`:2150`，merge 产物继承源 provenance）。**该机制当前不够完善健壮，需后续增强与优化**：

| 子问题 | 详情 | 风险 |
|--------|------|------|
| **merge 漏清孤立条目** | `cleanup_after_merge`（`master_agent.cpp:2406-2412`）清 `remote_idx_` / `local_idx_` 但**完全没碰 `write_provenance_`**。跨 path merge 后源 db_path 写 `_MIGRATED_TO` 重定向，源命名空间旧 provenance 条目成为孤立条目（不会再以源 path 写入） | 内存缓慢泄漏；频繁 merge 场景加剧 |
| **不持久化** | 进程重启后 `write_provenance_` 丢失（无 `save_to_file` / `load_from_file`），而 `remote_idx_` 可从 `.idx` 重建。重启后同一对象名被不同 context 重写时校验失效 | 破坏确定性保证（重启场景） |
| **无淘汰机制** | 随全局对象数线性增长，百万条约 80-100 MB；与 S1-3 重叠 | 内存膨胀 |
| **backup/merge hash 继承脆弱** | `master_agent.cpp:2060` / `:2150` 取 provenance hash 注入 backup/merge task，若 provenance 已丢失则继承失效 | 间接影响确定性 |

现有清理点（已覆盖路径）：`on_task_failed`（`:949`，按 `dirty_objects_` 清）、`on_object_removed`（`:1327`）、`broadcast_object_removed`（`:1347`）、`on_remove_request`（`:1487`）。**漏清路径**：merge `cleanup_after_merge`（`:2406`）。

增强方向（待启动，需进一步评审）：
- **修复 merge 漏清**：`cleanup_after_merge` 在清源 db_path 索引后，按 `db_path + ":"` 前缀遍历清理 `write_provenance_` 孤立条目（跨 path merge 时 target 命名空间同理，新 WriteRegister 会重建）。
- **持久化**：provenance 随 `.idx` / `_DB_META` 落盘，重启重建（与 `remote_idx_` 的 `rebuild_remote_idx_for_worker` 同构）。
- **配套对象生命周期的受控淘汰**：不裸 LRU（会破坏校验），仅在对象从 `remote_idx_` / `local_idx_` 真正消失时才允许清对应 provenance。

**[S4] TIER1 INCOMPLETE 状态无差别回退 TIER2 不合理**（2026-08-02 调研发现）— ✅ **关闭（2026-08-16 复核：非缺陷）**
- INCOMPLETE 本地写等待快路径已随 S1-2 落地（per-db 共享 cv + atomic predicate）。
- **FAILED 部分经复核不成立**：① `on_write_failed` 在同一 unique_lock 内 store FAILED 后立即 erase 条目——FAILED 态对并发读者竞争窗口为零，diag=2 的 FAILED 分支实际不可达；② wait 唤醒的读者重查为 not_found → TIER2 读旧副本是**正确语义**（对象曾 backup 时副本内容与 provenance hash 保证的幂等内容一致；若按原方向"FAILED 直接失败不回退"反而拒绝读有效副本，破坏正确性）；③ 无副本场景已闭环（TIER2 秒空 → TIER3 → `can_still_produce` 驱动 wait_obj 收敛）。既有测试锁定该行为（`data_service_test.cpp:1424` "FAILED object should return false (fallback to TIER2)"）。
- 遗留小项：`LocalObjectInfo.error_message_` 写入后条目立即 erase、无人读——死字段，归死代码清理。

读路径 `read_raw_compressed`（`data_service.cpp:1064`）TIER1 调 `try_read_local_raw`（`:714`），其对 `completion_state_` 的处理：

```
case COMPLETE + !is_temp → diag=3, 读 entries 落盘数据
case COMPLETE + is_temp  → diag=3, 读 temp_compressed_data_
case INCOMPLETE          → diag=2, 返回 {false, nullptr}  ← 回退 TIER2
case FAILED              → diag=2, 返回 {false, nullptr}  ← 同样回退（未区分）
```

TIER1 返回 false 时，`read_raw_compressed` 无差别进入 TIER2 远程读，对两种本应有不同处理的场景一视同仁：

| 场景 | 当前行为 | 问题 |
|------|----------|------|
| **本 worker 正在写（INCOMPLETE 是自己的异步写）** | 绕远程找其他副本；若无副本则 TIER3 回查 master（master 也没登记，因为 WriteRegister 在写完成后才上报）→ `can_still_produce` 退避轮询等到本 worker 自己写完 | 缺"等待本地写完成"快路径，本 worker 拥有最新数据却绕大圈；与 S1-2 per-object cv 被删后无本地等待机制强相关 |
| **写失败（FAILED）** | 当 INCOMPLETE 处理，回退 TIER2 找副本 | 语义不准；靠 `on_write_failed`（`data_service.cpp:326`）很快移除条目兜底，窗口小但存在 |

修复方向（待启动）：
- TIER1 区分 INCOMPLETE / FAILED：FAILED 直接返回失败（不回退远程）；INCOMPLETE 本地写则走"本地等待"快路径。
- 补"本地写完成 → 唤醒本地读者"信号：替代被删的 per-object cv（见 S1-2），用 per-db 共享 cv 或更轻量机制，避免本 worker 写时绕远程读。

**[S5] 存储层读写热路径 IO/syscall 放大优化** — ✅ **已完成（2026-08-03）**

> 来源：2026-08-02 分布式任务/文件系统架构性能瓶颈调研。经三层架构（storage/task/network）全面审查 + 8 份历史性能文档交叉核实，确认零拷贝优化（读写路径与数据面 wire 传输，历史分析文档已删、结论记录于 performance-analysis.md §0）、S1-2、S4 等已完成，识别出 3 个真实存在、未被历史优化覆盖、零架构改动的局部实现瓶颈，全部修复。

| 子项 | 问题 | 位置 | 状态 |
|------|------|------|------|
| **S5-1** | LocalIndex 每次 append（append_add/remove/marker）都 `std::ofstream ofs(idx_path_, app)` 重开文件 —— 每个 `write_object` 经 `commit_write → WBQ → write_record + flush → save → append_add` 触发 1 次 open/write/close syscall 组，批量写 N 个小对象 = N 次重开 | `local_index.cpp:99-148` | ✅ 已修复：持久 `idx_append_stream_` 成员复用，惰性打开，`save()` 末尾显式 flush 保 WAL 持久化语义，`compact/save_legacy` 的 truncate/rename 路径经 `reset_append_stream()` 重置 |
| **S5-2** | `do_read_raw_entries`（TIER1 ObjectCache miss 冷读路径）每次 `new DataReader`，构造函数 `index_->load()` 全量解析 `.idx` 文件构建 entries_ map，但调用方传入的 entry 已来自 `local_idx_` 内存索引，`read_raw_bytes(IndexEntry&)` 只用 db_path/data_path 定位文件，LocalIndex entries_ 完全没消费 —— 纯冗余 IO + 反序列化 | `data_service.cpp:618-622` + `data_reader.cpp:8-24` | ✅ 已修复：新增静态 `DataReader::read_raw_from_entry(entry, db_path, data_path)` 直接定位文件 + 区间读取，不构造 DataReader、不 load idx；`find_file_path`/`read_from_file` 重构为静态核心消除重复 |
| **S5-3** | `Database::reader_`（`CMUniquePtr<DataReader>`）是死成员 —— 构造时创建（触发一次 idx 全量 load + 持有 entries_ map 内存到析构），但全树 grep 确认其方法零调用 | `database.h:194` + `database.cpp:83` | ✅ 已修复：删除 reader_ 成员及构造。每个 Database 构造省去一次无用 idx 解析与常驻 LocalIndex 内存 |

**设计原则**：零架构改动、零并发模型变更、零协议改动。三个优化点都是局部实现层（单文件/单方法级），写序仍由 WriteBackQueue 单线程保证，读路径功能等价（entry 已知）。仅触碰 ObjectCache miss 的冷读路径与 idx 落盘路径，命中缓存的热对象不受影响。

**验证**：storage unit test 15/15、全量 cpp unit test 52/52、全量 QA 139/139、stability 50/50 零 crash。

**[S7] DataService 锁分片 + schedule 锁范围优化** — ✅ **已完成（2026-08-03）**

> 数据驱动：先建并发 benchmark 跑出优化前基线（揭示单 mutex 并发负伸缩），优化后跑对比数据验证提升量级。详见 [`docs/perf-baselines.md`](perf-baselines.md)（DataService 锁章节）。

| 子项 | 问题 | 方案 | 效果 |
|------|------|------|------|
| **S7-1** | DataService 单 `std::mutex` 保护所有数据域，多线程并发读被串行化，吞吐随线程数下降（负伸缩）：8 线程跨域 lookup 仅单线程 27%（948 vs 3516 ops/sec） | 拆 5 把 `std::shared_mutex`（local/remote/worker/db_paths/cb），读 shared_lock 并发、写 unique_lock 独占；跨域读双 shared_lock；cv 改 condition_variable_any；无数据冗余（worker_registry 地址唯一权威） | 8 线程提升 **16x**（场景 A：948→15226 ops/sec）；从负伸缩转正伸缩。详见基线文档对比表 |
| **S7-2** | schedule_tasks 在持 schedule_mutex_ 下查 DataService 预计算 locality hint，DataService 自带锁不依赖 schedule_mutex_ | locality 预计算移出锁外（锁外算 hint，持锁注入 graph + schedule_all_available） | 缩短 schedule_mutex_ 持锁时间（reactor 与 attr-tick 竞争窗口） |

**放弃的方案**：attr-tick 条件触发（仅限时 task 才触发 schedule_tasks）。实测破坏 attr timeout 语义——attr timeout 的 task 在 ready_tasks_（等匹配 worker）而非 pending_tasks_，按 pending 判断漏掉降级场景导致 task 卡死。attr-tick 无条件触发是语义必需（推进降级 + 死锁检测），不可加条件。

**设计要点**（回应用户反馈）：
- **无数据冗余**：不把 host/port 冗余进 RemoteObjectMeta，避免双份数据源漏改风险。跨域读用双 shared_lock（互相兼容并发），worker_registry 保持地址唯一权威。
- **cv 兼容**：std::condition_variable 只接受 unique_lock<std::mutex>，分片后 cv 改 std::condition_variable_any 配合 shared_mutex。wait 仅用于本地读等待写完成（非 DataServer 并发读热路径）。
- **reset 死锁修复**（前置）：锁外 drain/stop_write_back，逐域清空。消除持锁调 drain 与 WBQ 回调 on_write_completed 需锁的 AB-BA 死锁隐患。

**验证**：storage unit test 16/16（含新增 concurrency_bench）、master_agent + task 单测、全量 QA 139/139、stability 50/50 零 crash。

**[S8] task 调度热循环优化（H2-a 冗余重取 + H2-b get_ready_tasks sort）** — ✅ **已完成（2026-08-04）**

> 数据驱动：先建并发 benchmark 跑出优化前基线（揭示 O(N²) 退化），优化后跑对比数据验证提升量级。详见 [`docs/perf-baselines.md`](perf-baselines.md)（调度热循环章节）。

| 子项 | 问题 | 方案 | 效果 |
|------|------|------|------|
| **S8-1 (H2-a)** | select_best_worker 内重取 idle + 重建 idle_set（每 ready task 一次）；schedule_next 已取过，纯冗余 | select_best_worker 接受 idle_workers/idle_set 参数（const 引用），schedule_next 一次获取循环内复用。零架构改动 | 常数提升 ~5-10% |
| **S8-2 (H2-b)** | get_ready_tasks 每次 std::sort（O(R log R)），schedule_all_available 循环 N 次 → O(N² log N)，ready 积压时调度吞吐急剧退化（基线：1000t 仅 575 tasks/sec） | ready_tasks_ 从 unordered_set 改 std::set<pair<{-priority,task_id}>>，插入即有序，get_ready_tasks 删 sort 直接遍历。priority 不可变，key 稳定 | 1000t **53x**（575→30591），O(N²)→近线性 |

**优化前后对比**（vs 原始基线，H2-a+H2-b 合计）：
- 场景 A（大批次调度）1000t：575 → 30591 tasks/sec（**53.2x**）
- 场景 A（大批次调度）200t：3878 → 91739 tasks/sec（**23.7x**）
- 场景 B（get_ready_tasks）size=2000：275 → 23333 ops/sec（**84.8x**）
- O(N²) 退化彻底消除：1000t vs 50t 吞吐降从 35x 缩至 4.7x（近线性）

**架构变更说明**（H2-b，已与用户讨论确认）：ready_tasks_ 数据结构变更（unordered_set → std::set），封装在 DependencyGraph 内，外部 API 不变。priority 在 task 生命周期内不可变（add_task 时确定，无运行时 set_priority），故 set key 稳定。涉及 5 处维护点（add/check_and_move/remove/mark_data_removed/is_task_ready/get_ready_tasks）。

**验证**：task unit test 全绿（含 task_scheduler_test T1-T7 全套调度测试、dependency_graph_test）、master_agent 单测、全量 QA 139/139、stability 50/50 零 crash。

**[S2] db_id 废弃 — 改用 db_path + `_MIGRATED_TO` 迁移重定向**（2026-08-01 决策，详见 [`docs/adr/0002-deprecate-db-id.md`](adr/0002-deprecate-db-id.md)）

**背景**：`db_id`（10 字符 base62）最初引入是为了缩短 db 唯一标识符、节省内存（早期 idx 每条记录存 `db_id:short` 全名）。经 2026-08-01 调研核实：内存层 `local_idx_`/`remote_idx_` 已用嵌套 map 良好归类，db_id 只作每 db 一份的外层 key，**不随对象数膨胀**；真正随对象数增长的内存项是 `LocalObjectInfo` 的 mutex+cv（S1-2）和 `remote_idx_` 无上限累积（S1-3），与标识符无关。db_id 的唯一不可替代角色是"跨 db 依赖的逻辑锚点"——merge 改物理路径时让 object_name 保持稳定。

**决策**：废弃 db_id，改用 `db_path`（base_path）作 db 唯一标识符。用"源 path 永久保留 + `_MIGRATED_TO` 迁移指针文件"替代 db_id 作为稳定锚点：
- merge 跨 path 时在源 base_path 写 `_MIGRATED_TO`（指向 target），源目录保留 `_DB_META`/`_FROZEN`/`_MIGRATED_TO`，删除 `.dat`/`.idx`
- `DataService::resolve_migrated_path` 单点重定向解析（含缓存），Database 构造 / register_database 入口调用
- `full_name = "db_path:short"`（冒号拼接），split 用 `rfind(':')`；base_path 在创建时校验不含 `:`（双保险）
- 默认 merge（base_path 不变）零迁移开销，只 data_path 变
- ADR 0001 第 4 条（db_id 持久化）**作废**：idx 改存 short_name 后不再编码 db 标识，mismatch 链条已断

**收益**：消除 db_id 生成/碰撞检测/定长 split/db_id↔path 映射等复杂度；idx 文件 name 部分约减 30-50%（S1-1）；标识符回归天然唯一（path）。

### ⏸️ 降级 / 待环境

| 项 | 状态 | 解锁条件 |
|---|---|---|
| F1 SSH 多机 | 功能已具备，待验证 | 多机/容器化测试环境就绪 |
| F2 Freeze 聚合 | 降级 | 出现 load_db worker 不齐备的真实痛点时（设计方案见 [`db-merge-design.md`](db-merge-design.md)） |
| F4 大对象分片 | 降级 | 出现单对象传输成为瓶颈的实测证据时 |

### ⛔ 明确不做（本阶段）

- F6 stage checkpoint（db 级足够）
- F7 协议版本号（早期无需）
- 竞品分析 §5.4/§5.5 全部否决项（去中心化调度 / 通用自动重试 / durable execution 重放 / Saga / Actor 模型）——战略边界，永久不做

---

## 六、更新文档清单

执行 P0 后需同步：

1. ~~locality-scheduling-plan.md / locality-scheduling-review.md 状态标注~~（2026-08-16 已随任务完结删除，历史见 git）
3. `docs/competitor-analysis.md` — §1.3 修正 locality 状态（从"未实现"改为"已实现"）
4. `CLAUDE.md` / `docs/architecture.md` — task 模块依赖描述（移除 storage 依赖）

---

*文档制定日期：2026-06-30*
