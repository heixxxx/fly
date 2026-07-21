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
| **A1** | locality 实现违反六层分层：`TaskScheduler`（Layer 3）向下依赖 `DataService`（Layer 1） | `task_scheduler.h:4 #include <storage/cpp/data_service.h>`；`src/task/cpp/BUILD:16 deps += fly_storage` |

> 这是本路线图的**首要技术交付**，独立成节见 §3。

### 2.2 功能空缺

| 编号 | 短板 | 证据 | 处置 |
|---|---|---|---|
| **F1** | SSH / 多机 Worker 启动 | `launch_ssh_workers` 全仓库零命中；仅 `subprocess.Popen` 本机 | **功能已具备，降级**：见 §4 决策记录 ① |
| **F2** | Freeze 后处理（idx 合并 / merged.idx / _META 聚合） | master 无 `IdxRequest` handler；`grep merged.idx src/` 零命中 | **降级**：见 §4 决策记录 ② |
| **F3** | Worker role 调度（storage_only / hybrid 差异化） | `_spawn_process_worker` 忽略 role 字段；task_scheduler 不读 role | 保持 P1 |
| **F4** | 大对象分片传输 + 背压 | DataResponse 两段式但不分片；仅连接池并发限流，无 credit 流控 | **降级**：见 §4 决策记录 ⑤ |
| **F5** | 任务优先级 | `TaskRequirements` 无 priority 字段；纯 FIFO | 保持 P1 |
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
5. 同步修订 `locality-scheduling-plan.md` §2.1 原则 6 与 `locality-scheduling-review.md` P0 项状态。

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
| ⑧ | P1：Worker role 调度（F3） | **保持** | storage_only role 需落地 |

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

**[F5] 任务优先级**
- `TaskRequirements` 加 `int priority_ = 0`
- `schedule_next()` 的 ready_tasks 按 priority 降序排序后再遍历
- 改动小、收益明确（多流程并行 / 调试抢占）

**[F3] Worker role 调度**
- `_spawn_process_worker` 消费 role 字段传给 worker
- `TaskScheduler` 阶段 A 增加规则：`storage_only` worker 不接收计算任务
- 配套 `requires` 语义：默认任务隐式要求 `hybrid` role

**[M1] 数据对象元信息的内存上限（观察项）** — 详见 [`memory-growth-analysis.md`](memory-growth-analysis.md)
- **现状**：`remote_idx_` / `local_idx_` / `write_provenance_` 只在显式 `remove_object` 时清理，无数量上限/TTL/LRU，随对象数线性增长。task 侧已有完善上限（`kMaxCompletedTasks=100` + 完成即清理），数据对象侧没有。
- **触发阈值**：全局对象 >100 万、或 master 内存 >500MB 成为真实痛点时启动
- **方案方向**：给 `remote_idx_` 加 `max_entries` 配置 + LRU 淘汰（按 `last_access_time_`，数据仍在磁盘仅内存索引换出）；`write_provenance_` 同步淘汰。与 ObjectCache 的 LFU 模式同构，可复用经验
- **不做**：不给 `local_idx_`（worker 自写对象索引）加淘汰——它是磁盘 idx 的内存镜像，淘汰会破坏写/读语义
- **当前结论**：十几小时 / 十万级对象的典型 run，内存增长在几十 MB 量级，**可接受，不阻塞**

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

1. `docs/locality-scheduling-plan.md` — §2.1 原则 6 状态标注"已落地"
2. `docs/locality-scheduling-review.md` — §1 P0、§5 P0 项标记 RESOLVED
3. `docs/competitor-analysis.md` — §1.3 修正 locality 状态（从"未实现"改为"已实现"）
4. `CLAUDE.md` / `docs/architecture.md` — task 模块依赖描述（移除 storage 依赖）

---

*文档制定日期：2026-06-30*
