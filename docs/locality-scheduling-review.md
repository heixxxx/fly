# Data Locality 调度实现 — Code Review

> 对应计划：[`locality-scheduling-plan.md`](locality-scheduling-plan.md)
> Review 范围：`git diff HEAD` 全部改动（23 文件，+544/-233 行）
> 评审日期：2026-06-28

---

## 0. 总览

实现总体**忠实于计划文档**：

- 工作流 A（统一 `WriteRegisterMessage`、删除 `DataReadyMessage`）执行干净彻底，无残留引用。
- 工作流 B（size 链路）覆盖全部四条写入路径（`commit_write` / `do_backup_write` / `put_temp_data` / master 自写）。
- 工作流 D（scheduler 三阶段算法）与"capability 强约束优先、locality 不降低能力质量"的不变量吻合。

但存在**一处架构级偏离**（工作流 C 未实现）及若干性能/测试/风格问题，按优先级整理如下。

---

## 1. 功能实现 — 与计划的偏离

### 🔴 P0 严重：工作流 C 完全未实现，scheduler 反向依赖 Storage

**这是本次 review 最关键的发现。**

计划 §2.3 工作流 C/D 的核心设计原则（§2.1 原则 6）：

> **scheduler 不接触 DataService** —— master 预计算 locality hint，scheduler 只消费 `TaskRequirements.locality_order_`（POD）。

**实际实现完全偏离：**

| 计划要求 | 实际状态 |
|---------|---------|
| `MasterAgent::compute_locality_order(task_id)`（§2.3 C1） | ❌ 未实现，grep 全仓库零命中 |
| `TaskRequirements` 加 `locality_order_` 字段（§2.3 C2） | ❌ 未实现 |
| `DependencyGraph::set_task_locality_hint` setter | ❌ 未实现 |
| `task_scheduler.h` 不 include storage | ❌ 第 4 行 `#include <storage/cpp/data_service.h>` |
| task 模块不依赖 storage 模块 | ❌ `BUILD` 给 `fly_task` 加了 `fly_storage` 依赖 |

scheduler 改成在 `select_best_worker` 里**自己调** `DataService::instance()->get_remote_workers/get_remote_size`（`task_scheduler.cpp:131-134`）现算分数。

**为什么是问题：**

1. **破坏分层架构** —— task 是 Layer 3，storage 是 Layer 1，scheduler 现在向下依赖 storage。
2. **测试可隔离性丧失** —— 计划 §Step 8c 设计用 fake DependencyGraph 测试"不依赖真实 DataService"。实际单测 `task_scheduler_test.cpp:594` 注释仍写着"不依赖真实 DataService"，但 T1–T5 每个都在真实调 `DataService::instance()->update_remote_idx(...)`（如 T2 `task_scheduler_test.cpp:626`）—— **测试注释与实现自相矛盾**。
3. **计划风险缓解未兑现** —— §4.1 风险表专门列了 "分层依赖（task → storage）"，缓解方式是 "scheduler 不 include storage"。这个缓解没做。

**建议（二选一）：**

- **方案 A（回归设计）**：补齐工作流 C —— 实现 `compute_locality_order` + `locality_order_`，scheduler 改回只读 POD hint，移除 task → storage 依赖。
- **方案 B（修订计划）**：评估后认为让 scheduler 直接查 DataService 更简单，**显式修订计划文档**说明放弃预计算 hint 设计，相应更新原则 6 和 §4 风险表，并修复测试注释。

> ⚠️ 这是架构级决策，影响后续重构方向和测试架构，建议先与项目维护者确认走向后再动手。

### 🟡 P2：master 自写返回值语义混乱

`master_agent.cpp:1543`：

```cpp
auto ack = do_write_register(msg);
if (!ack.success_) {
    return {ack.error_message_, ack.error_type_};
}
return {"", TaskErrorType::UNKNOWN};  // ← 成功时返回 UNKNOWN？
```

`TaskErrorType::UNKNOWN` 在 codebase 里是"无错误"哨兵（多处 `if (err_type != UNKNOWN)` 判断失败），但字面意思"未知错误"。成功路径返回名叫 UNKNOWN 的枚举，可读性差。沿用旧风格非回归，但既然在重构这块逻辑，建议加注释说明 `UNKNOWN==成功哨兵`，或改用语义更清晰的枚举值。

### 🟡 P2：内部 backup task 的 size 链路时序依赖隐式

`worker_agent.cpp:1199`：internal backup task 的 `TaskCompleteMessage.written_objects_` push `{name, 0}`，注释说"真实 size 已由 `do_backup_write` 的 register 路径登记"。

`on_task_complete` 的 `is_internal_` 分支（`master_agent.cpp:830`）调 `update_remote_idx(..., wo.size_bytes_=0)`，因 `size_bytes==0` 而**保持原值不变**——即隐式依赖 backup 的 register 阶段**先**登记真实 size。这个时序依赖是隐式的，建议在 `worker_agent.cpp:1199` 注释里点明。

---

## 2. 性能

### 🟡 P1：`compute_scores` 内层线性查找退化为 O(W²)

`task_scheduler.cpp:131-143`：

```cpp
for (const auto& obj : deps) {
    auto holders = ds->get_remote_workers(obj);
    int64_t sz = ds->get_remote_size(obj);
    for (uint64_t h : holders) {              // 外层：holders
        for (auto& entry : score_buf_) {       // 内层：线性扫描 score_buf_
            if (entry.worker_id == h) { ... break; }
        }
    }
}
```

注释声称"复杂度 O(deps × avg_holders)"，实际是 **O(deps × holders × num_workers)**。每次给 holder 加分都线性扫一遍 `score_buf_`。holder 数 ≈ worker 数时退化为 O(deps × W²)。

计划 §2.3(C1) 伪代码用 `CMUnorderedMap<uint64_t,int64_t> score`，holder 查找 O(1)。当前实现为"复用持久缓冲区避免 per-task 分配"牺牲了复杂度——复用缓冲区完全可以用 `CMUnorderedMap`（clear 后重填，无堆分配），不需要线性查找。

**建议**：`score_buf_` 改为 `CMUnorderedMap<uint64_t,int64_t>`，或配 worker_id→index 辅助 map，消除内层线性扫描。

### 🟢 亮点：Config 运行时同步开关

`master_agent.cpp:400-402` 每次 `schedule_tasks()` 前从 Config 同步 `locality_scheduling_enabled`，正确修复了计划 §6.3 发现的"构造时只读一次"问题，运行时 `set_int` 即时生效。

### 🟢 亮点：master 自写零网络开销

`on_master_register_write` 同步调 `do_write_register` 并丢弃 ack（`master_agent.cpp:1543`），避免"给自己发网络消息"的开销，符合计划 §2.3(A1) 的 R4 缓解方案。

---

## 3. 模块划分

### 🔴 P0：（同 §1）task → storage 的非法反向依赖

`src/task/cpp/BUILD` 给 `fly_task` / `fly_task_so` 都加了 `fly_storage` 依赖，`task_scheduler.h` include `<storage/cpp/data_service.h>`。详见 §1 P0 项。

### 🟢 亮点：`do_write_register` 纯逻辑提取

把 `on_write_register` 核心逻辑抽成纯函数 `do_write_register(msg) -> ack`，worker 路径（回 ACK）和 master 自写路径（丢弃 ACK）共用，是干净的职责分离，完全符合计划 §2.3(A1)。

### 🟢 亮点：`record_worker_info` / `evaluate_and_trigger_backup` 提取

从原 `on_data_ready` 的庞大函数体里抽出两个有明确职责的私有方法，消除删除 `DataReadyMessage` 后的逻辑冗余，可读性大幅提升。

### 🟡 P2：Config 同步位置略尴尬

`master_agent.cpp:400` 在 `schedule_tasks()` 开头同步 `locality_scheduling_enabled` 到 scheduler，把"配置变更感知"塞进调度入口。更干净的做法是 Config 加变更回调或 scheduler 持 Config 引用。考虑到软开关、调用频率不高，当前方案可接受。

---

## 4. 代码风格

### 🟡 P2：缩进不一致

`worker_agent.cpp:513-516` 方法体用了 5 空格缩进（多 1 空格），与文件其余 4 空格不一致，改动引入：

```cpp
void WorkerAgent::record_write(const CMString& db_id, const CMString& object_name, int64_t size) {
     CMString full_name = db_id + ":" + object_name;   // ← 5 空格
     current_writes_.push_back(full_name);
     current_write_sizes_[full_name] = size;
}
```

### 🟡 P2：未使用的 `empty_caps_` 成员

`task_scheduler.h:43`：

```cpp
static inline const CMVector<CMString> empty_caps_{};
```

grep 全文件无引用，死代码，应删除。

### 🟡 P2：`select_best_worker` 两段重复 sort lambda

`task_scheduler.cpp:168-177`（caps 为空）和 `201-205`（caps 非空）用了**完全相同**的 sort lambda（score 降序 + worker_id 升序）。可抽静态比较器或 helper 消除重复。

### 🟡 P2：阶段 A/B 逻辑分支分散

`select_best_worker` 在 `caps.empty()` 时提前 return（`:165-180`），导致 `best_partial_count` 只在 caps 非空时计算，阶段 B 又分 caps 空/非空两套分支。建议统一流程：先算所有 idle worker 的 capability 匹配数 + locality score，再按统一规则选，减少特判。

### 🟢 亮点：注释质量高

`do_write_register`、`record_worker_info`、`on_master_register_write` 注释清楚说明了"从哪抽出、为什么改、与 worker 路径的对称性"，符合 codebase 重视注释的风格。

---

## 5. 测试覆盖

### 🟢 亮点：单测 T1–T6 覆盖关键不变量

- **T4**（capability 完整匹配优先于 locality）+ **T5**（locality 不降低 capability 质量）直接守护计划 §2.3(D1) 核心不变量，覆盖最易反优化处。
- **T1**（disabled 行为不变）回归保护；**T6**（无输入退原行为）覆盖 R3 空集合语义。

### 🟢 亮点：QA 三层测试齐全

| 测试 | 维度 |
|------|------|
| `test_locality_basic.py` | 功能正确性（2 worker，consume 落持有者） |
| `test_locality_capability_priority.py` | capability 优先 E2E |
| `test_locality_perf.py` | 性能量化（3 worker 各 24MB，断言 `local_hits==3`） |

覆盖维度完整，性能测试有硬断言 + 实测数据（计划 §6.2）。

### 🔴 P0：测试架构与设计原则背离（同 §1）

单测注释声称"不依赖真实 DataService，用 fake DependencyGraph"，实际直接调真实 `DataService::instance()`。后果：

- 测试间**共享全局 singleton 状态**，T1–T5 靠每个 case 手动 `remove_remote_index` 清理——任一忘记清理即污染后续，脆弱。
- 计划设计的"scheduler 只消费 POD hint、可纯单测"的可测试性优势**完全没兑现**。

若坚持当前实现（scheduler 直接查 DataService），至少在每个测试 setup/teardown 里 `DataService::instance()->reset()`，而非依赖手动清理。

### 🟡 P1：缺失三类高风险迁移点的单测

| 缺失测试 | 计划要求 | 风险 |
|---------|---------|------|
| `update_remote_idx` size==0 不覆盖语义 | Step 2e | §4.1 专门列为风险，防御 rebuild 路径 |
| master 自写 auto-backup 迁移 | Step 4d | 删 `DataReadyMessage` 时风险最高迁移点（§4.1 列两条风险） |
| backup 副本 size 幂等 | §4.1 | "副本 size 必须等于原对象 size"无测试守护 |

现有 `master_agent_test.cpp` 只测了 `has_remote_location` / `get_remote_size`，未覆盖 auto-backup 触发和 `recorded_workers_` 登记的迁移正确性。

---

## 6. 问题汇总与优先级

| 优先级 | 维度 | 问题 | 建议 |
|--------|------|------|------|
| 🔴 P0 | 功能/模块 | 工作流 C 未实现，scheduler 反向依赖 storage | 补齐 `compute_locality_order`/`locality_order_` 回归设计，或修订计划文档 |
| 🔴 P0 | 测试 | 单测实际依赖真实 DataService，与设计原则/注释矛盾 | 随 P0 修复改回 hint 方案，或加 `DataService::reset()` 隔离 |
| 🟡 P1 | 性能 | `compute_scores` 内层线性查找 O(W²) | 改用 unordered_map 索引 |
| 🟡 P1 | 测试 | 缺 size==0 不覆盖 / master 自写 backup / backup 幂等三类单测 | 补单测守护高风险迁移点 |
| 🟡 P2 | 功能 | master 自写返回 `UNKNOWN` 语义混乱 | 加注释或改枚举值 |
| 🟡 P2 | 功能 | internal backup task size 时序依赖隐式 | 注释点明依赖关系 |
| 🟡 P2 | 模块 | Config 同步塞进调度入口 | 可接受，或改 Config 回调 |
| 🟡 P2 | 风格 | `record_write` 5 空格缩进 | 改回 4 空格 |
| 🟡 P2 | 风格 | `empty_caps_` 死代码 | 删除 |
| 🟡 P2 | 风格 | `select_best_worker` 两段重复 sort lambda | 抽 helper |
| 🟡 P2 | 风格 | 阶段 A/B 分支分散特判 | 统一流程 |

---

## 7. 结论

P0 不是"实现有 bug"——核心功能正确、性能收益实测有效、消息统一改造彻底。**P0 是实现与计划文档在设计原则上的根本性分歧**（分层依赖、预计算 hint vs 现场查表），而文档没有任何修订说明。

**建议下一步**：先就"是否回归工作流 C 的设计"做决定。这是影响后续重构方向和测试架构的架构级决策，值得先对齐再动手——P0 两项（模块依赖 + 测试架构）会一起随之解决，P1/P2 可在确定方向后批量清理。
