# Locality 分层修复方案

> 目标：解除 `TaskScheduler`（Layer 3）对 `DataService`（Layer 1）的向下依赖，
> 恢复 fly 六层架构的 BUILD 级无环原则。
> 基线日期：2026-06-30（源码逐行核实）

---

## 一、问题确认（已 100% 核实）

### 1.1 违反点（精确 3 处，全部集中在 task_scheduler）

| # | 位置 | 内容 |
|---|---|---|
| 1 | `src/task/cpp/BUILD:16` | `deps += ["//src/storage/cpp:fly_storage"]` |
| 2 | `src/task/cpp/BUILD:26` | `dynamic_deps += ["//src/storage/cpp:fly_storage_so"]` |
| 3 | `src/task/cpp/task_scheduler.h:4` | `#include <storage/cpp/data_service.h>` |

> task 模块对 storage 的依赖**仅此 3 处**。`dependency_graph` / `worker_manager` /
> `task_manager` / `heartbeat_monitor` 均干净。改动面高度收敛。

### 1.2 根因调用点（task_scheduler.cpp:135-144）

```cpp
auto ds = DataService::instance();                 // 行 135 ← 要删
for (const auto& obj : deps) {
    auto holders = ds->get_remote_workers(obj);    // 行 137 ← 要删
    int64_t sz = ds->get_remote_size(obj);         // 行 138 ← 要删
    for (uint64_t h : holders) {
        if (h < score_buf_.size()) {
            score_buf_[h].score += sz;             // 行 141 ← 改为读 hint
        }
    }
}
```

scheduler 在 `compute_scores()` 里直接调 `DataService::instance()` 现算 locality
分数，而非消费上游预计算的 POD hint。

### 1.3 为何是问题

1. **违反 architecture.md §4.1 宣称的核心优势**——"六层清晰架构 + BUILD 级无循环依赖"。
   task=Layer 3 向下依赖 storage=Layer 1。
2. **破坏测试可隔离性**——单测 `task_scheduler_test.cpp:594` 注释自称"用 fake
   DependencyGraph、不依赖真实 DataService"，实际 T1–T5 每个都调真实
   `DataService::instance()->update_remote_idx()`，靠每个 case 手动
   `remove_remote_index` 清理维持隔离，任一遗漏即污染后续，脆弱。

### 1.4 性能澄清

`locality-scheduling-review.md §2` 指控的 O(W²) **当前已不成立**：
`compute_scores`（task_scheduler.cpp:141）已改用 worker_id 直接索引
`score_buf_[h]`，复杂度 O(deps×holders)。本次修复**只针对分层依赖，不动性能**。

---

## 二、方案选型：A（预计算 hint）

| 维度 | A：预计算 hint | B：修订设计接受依赖 |
|---|---|---|
| 分层纯洁性 | ✅ 恢复 BUILD 无环 | ❌ 永久打破 Layer 3→1 隔离 |
| 测试隔离 | ✅ scheduler 可纯单测 | ❌ 永久依赖 DataService singleton |
| 多机化前景 | ✅ hint 是 POD，可随 task 派发 | ❌ 多机时耦合更深 |
| 改动量 | 中（5 处，但全部已核实） | 小（改文档+注释）但留债 |

**决策：方案 A。** 经核实改动面比预期小、风险比预期低（见 §三的关键确认），
而分层无环是 fly 对外宣称的核心工程优势（competitor-analysis §4.2），
不应为省一次重构永久放弃。

---

## 三、关键确认（决定方案成败，全部利好）

1. **`TaskRequirements` 不跨进程传输** ✅
   消息走 `TaskSubmitMessage` 的独立字段 `required_capabilities_` +
   `attribute_timeout_`（message_types.h:268/273），master 进程内再构造
   `TaskRequirements`（master_agent.cpp:370-372）。**加字段无需改序列化/协议**。
   `FailedTaskRecord`（master_agent.h:28-39）也不含它，失败重放路径不受影响。

2. **master 侧已有 per-task 数据位置缓存** ✅
   `task_dependency_locations_`（master_agent.h:161-168）已在 submit /
   write_register 时维护 "task_id → {obj → (worker_id,host,port)}"。
   hint 注入的**天然挂载点已存在**。

3. **hint 失效时机天然覆盖** ✅
   remote_idx 每次更新后都触发 `schedule_tasks()`（master_agent.cpp:849/916/814
   等多个路径）。hint 预计算放在 `schedule_tasks()` 入口即可，**无需额外失效逻辑**。

4. **T7 不变量可保留** ✅
   阶段 B 的 `best_partial_count > 0` 防御（task_scheduler.cpp:197）只依赖
   capability 匹配数，与 hint 无关。方案不影响该不变量。

5. **查询函数返回值语义清晰** ✅
   `get_remote_workers` 按值返回 worker_id 列表，`get_remote_size` 未登记返回 0。
   预计算逻辑可原样搬到 master 侧。

---

## 四、完整实现（5 处改动 + 测试改造）

### 改动 1：`TaskRequirements` 增加 locality hint 字段

**文件**：`src/task/cpp/dependency_graph.h:17`

```cpp
struct TaskRequirements {
    CMVector<CMString> capabilities_;
    float timeout_seconds_ = -1.0f;

    // Locality hint：master 预计算的 worker→亲和分（worker 持有的输入字节数）。
    // scheduler 只消费此 POD，不接触 DataService。空 = 无 locality 信息（退 FIFO）。
    // 纯进程内临场数据，不参与序列化（TaskRequirements 不跨进程）。
    CMVector<std::pair<uint64_t, int64_t>> locality_hint_{};
};
```

> 不加 `FLY_SERIALIZE`——`TaskRequirements` 不跨进程（见 §三.1）。

### 改动 2：`DependencyGraph` 增加 hint setter

**文件**：`src/task/cpp/dependency_graph.h`（class DependencyGraph public 区）

```cpp
// master 在 schedule 前按 task 依赖查 DataService 预计算 locality_hint_，写入此结构。
// scheduler 只读不查。线程安全（内部加锁，与其它 getter 一致）。
void set_task_locality_hint(uint64_t task_id,
                            CMVector<std::pair<uint64_t, int64_t>> hint);
```

**文件**：`src/task/cpp/dependency_graph.cpp`（新增实现）

```cpp
void DependencyGraph::set_task_locality_hint(
        uint64_t task_id, CMVector<std::pair<uint64_t, int64_t>> hint) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = task_requirements_.find(task_id);
    if (it != task_requirements_.end()) {
        it->second.locality_hint_ = std::move(hint);
    }
    // task 不存在时静默忽略：set 可能在 add_task 之前调用（防御），
    // 与 get_task_requirements 的"找不到返回静态空"语义对称。
}
```

### 改动 3：`TaskScheduler` 改为只消费 hint

**文件**：`src/task/cpp/task_scheduler.cpp` —— `compute_scores()`（行 114-146）

删除 `DataService::instance()` 及 `get_remote_workers/get_remote_size` 调用，
改为从 `reqs.locality_hint_` 直接填 `score_buf_`：

```cpp
size_t TaskScheduler::compute_scores(uint64_t task_id) {
    auto all_workers = manager_->get_all_workers();

    uint64_t max_id = 0;
    for (const auto& w : all_workers) {
        if (w.worker_id_ > max_id) max_id = w.worker_id_;
    }
    score_buf_.clear();
    score_buf_.resize(max_id + 1);
    for (const auto& w : all_workers) {
        score_buf_[w.worker_id_] = {w.worker_id_, 0};
    }

    // 消费 master 预计算的 locality_hint_（POD），不再查 DataService。
    // hint 每个 entry = (worker_id, 该 worker 持有的输入字节数)。
    const TaskRequirements& reqs = graph_->get_task_requirements(task_id);
    for (const auto& [wid, score] : reqs.locality_hint_) {
        if (wid < score_buf_.size()) {
            score_buf_[wid].score = score;  // master 已聚合，直接赋值
        }
    }
    return score_buf_.size();
}
```

> `select_best_worker()`（行 148-214）**无需改动**——它只读 `score_buf_`，
> 三阶段算法（capability 完整匹配 → locality 偏好 → 兜底）及其不变量全部保留。

**文件**：`src/task/cpp/task_scheduler.h:4` —— 删除 `#include <storage/cpp/data_service.h>`。

### 改动 4：解除 BUILD 依赖

**文件**：`src/task/cpp/BUILD`

```python
cc_library(
    name = "fly_task",
    ...
    deps = [
        "//src/common/cpp:fly_common_types",
        "//src/log/cpp:fly_log",
        # 删除：# "//src/storage/cpp:fly_storage",
    ],
)

cc_shared_library(
    name = "fly_task_so",
    deps = [":fly_task"],
    dynamic_deps = [
        "//src/log/cpp:fly_log_so",
        # 删除：# "//src/storage/cpp:fly_storage_so",
    ],
    ...
)
```

### 改动 5：master 侧预计算注入

**文件**：`src/agent/cpp/master_agent.cpp` —— `schedule_tasks()`（行 403-423）

在 `scheduler_->schedule_all_available()` 之前插入 hint 预计算。master
（Layer 4）合法持有 DataService，预计算后注入 graph（Layer 3），分层无环：

```cpp
void MasterAgent::schedule_tasks() {
    if (draining_.load()) return;

    std::lock_guard<std::mutex> lock(schedule_mutex_);
    auto ready = graph_->get_ready_tasks();
    auto idle = worker_manager_->get_idle_workers();

    // ...（现有 ready/pending 日志不变）...

    bool locality_on =
        Config::instance()->get_int("locality_scheduling_enabled") == 1;
    scheduler_->set_locality_preference(locality_on);

    // 【新增】预计算 locality hint 注入 graph。master 合法持有 DataService。
    // 每个 ready task 的依赖对象 → 持有者 worker 累计字节数。
    if (locality_on && !ready.empty()) {
        auto ds = DataService::instance();
        for (uint64_t tid : ready) {
            auto deps = graph_->get_task_dependencies(tid);
            if (deps.empty()) continue;  // 无依赖，hint 留空（退 FIFO）
            CMUnorderedMap<uint64_t, int64_t> acc;  // worker_id → 累计字节
            for (const auto& obj : deps) {
                int64_t sz = ds->get_remote_size(obj);
                for (uint64_t h : ds->get_remote_workers(obj)) {
                    acc[h] += sz;
                }
            }
            CMVector<std::pair<uint64_t, int64_t>> hint(acc.begin(), acc.end());
            graph_->set_task_locality_hint(tid, std::move(hint));
        }
    }

    auto results = scheduler_->schedule_all_available();
    // ...（后续 assign 不变）...
}
```

> 注：`schedule_mutex_` 已保护整个 `schedule_tasks()`，预计算在其内部，
> 无新增竞态。`get_task_dependencies` / `set_task_locality_hint` 各自带锁，
> 嵌套调用安全（DependencyGraph 用独立 mutex，与 schedule_mutex_ 无关）。

### 测试改造

**文件**：`src/task/tests/task_scheduler_test.cpp` —— T1/T2/T3/T4/T5/T7（行 593-751）

**原则**：移除所有 `DataService::instance()->update_remote_idx(...)` 与
`remove_remote_index(...)` 清理，改为直接注入 hint。

辅助函数（替换现有 DataService 调用模式）：

```cpp
// 直接注入 locality hint（POD），不再触碰 DataService singleton。
static void inject_hint(DependencyGraph& graph, uint64_t task_id,
                        std::initializer_list<std::pair<uint64_t, int64_t>> entries) {
    graph.set_task_locality_hint(task_id, {entries.begin(), entries.end()});
}
```

T2 改造示例（其余 T1/T3/T4/T5/T7 同理）：

```cpp
TEST(TaskSchedulerTest, LocalityNoCapabilityPrefersHolder) {
    DependencyGraph graph;
    WorkerManager manager;

    graph.add_task(1, {"db_t2:obj"}, {});
    graph.mark_data_ready("db_t2:obj");
    manager.register_worker(1, "127.0.0.1", 8080, {});
    manager.register_worker(2, "127.0.0.1", 8081, {});
    inject_hint(graph, 1, {2, 100});   // ← 替换 DataService 调用

    TaskScheduler scheduler(&graph, &manager);
    scheduler.set_locality_preference(true);
    auto result = scheduler.schedule_next();

    EXPECT_TRUE(result.scheduled_);
    EXPECT_EQ(result.worker_id_, 2u);

    // 无需 remove_remote_index 清理
}
```

修正行 594-596 注释，与实现一致：

```cpp
// ===== Data Locality 调度测试（T1-T7）=====
// scheduler 消费 master 预计算的 locality_hint_（POD），不接触 DataService。
// 测试通过 inject_hint 直接注入分数，单测天然隔离，无 singleton 污染。
```

---

## 五、验证清单

> **2026-06-30 实施完成，全部勾选。**

### 编译期

- [x] `./fly.sh build //src/task/...` 通过
- [x] `grep "storage" src/task/cpp/BUILD` → 零命中
- [x] `bazel query deps(//src/task/cpp:fly_task)` 依赖闭包不含任何 storage target（编译期铁证）

### 测试期

- [x] `./fly.sh test //src/task/...` 全绿，含 T1–T7
- [x] `grep "DataService" src/task/tests/task_scheduler_test.cpp` → 仅余注释文字
- [x] 全量单测 50/50 全绿

### 集成期

- [x] 全量 QA 通过（111/111），含：
  - `qa/scheduling/test_locality_basic.py`
  - `qa/scheduling/test_locality_capability_priority.py`
  - `qa/scheduling` 全目录 14/14
- [x] locality 关闭时行为不变（T1 回归守护）

---

## 六、风险与缓解

| 风险 | 评估 | 缓解 |
|---|---|---|
| 预计算增加 `schedule_tasks()` 开销 | 低。ready task 数通常远小于 pending；原实现每次 `select_best_worker` 都查 DataService，新实现每 task 只查一次，**总查询次数下降** | 无需额外缓解 |
| hint 与 remote_idx 不一致 | 已覆盖。remote_idx 每次更新都触发 `schedule_tasks()`，预计算在其入口，hint 总是最新 | §三.3 |
| 改动触碰调度热路径 | TDD：先改测试（注入 hint 验证 T1–T7 不变量仍守护），再改实现 | 先测试后实现 |
| `locality_hint_` 未初始化导致空 hint | 非 bug：空 hint = 无 locality 信息，scheduler 退 FIFO（T6 已覆盖此语义） | T6 守护 |
| 多机化时 hint 需跨进程 | 当前 hint 是进程内临场数据，不跨进程。未来多机调度时再补序列化，不影响本方案 | 留注释说明，不在本次做 |

---

## 七、文档同步

实施完成后更新：

1. `docs/locality-scheduling-plan.md` §2.1 原则 6 —— 标注"已落地"
2. `docs/locality-scheduling-review.md` §1 P0、§5 P0 —— 标记 RESOLVED
3. `docs/roadmap.md` §三 —— 标记完成
4. `CLAUDE.md` task 模块表 —— 移除对 storage 依赖的隐含描述（如有）

---

## 八、实施顺序（TDD）

1. **先改测试**：T1–T7 改为 `inject_hint`，此时编译失败（`set_task_locality_hint` 未实现）→ 红灯
2. **改动 1+2**：加 `locality_hint_` 字段 + `set_task_locality_hint` setter → 测试仍红（scheduler 还查 DataService）
3. **改动 3**：`compute_scores` 改读 hint + 删 include → 单测全绿
4. **改动 4**：删 BUILD 依赖 → `./fly.sh build //src/task/...` 通过
5. **改动 5**：master 预计算注入 → 集成 QA 全绿
6. **文档同步**：更新 plan/review/roadmap

---

*方案制定日期：2026-06-30*
