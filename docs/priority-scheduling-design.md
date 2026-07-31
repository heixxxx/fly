# Task Priority（任务优先级）设计与实现方案

> 状态：**已实现**（TDD，2026-07-31）
> 制定日期：2026-07-31
> 关联：`docs/roadmap.md` §五 [F5]（✅ 已完成）；`docs/architecture.md` §3.2（任务调度策略）

---

## 0. 摘要

为 `TaskRequirements` 增加 **`int priority_`** 字段（默认 10），使 scheduler 在多个 ready task 竞争有限 worker 时，**优先调度 priority 高的 task**。此前 scheduler 纯 FIFO（按 task_id 升序），无法表达"多流程并行时某条流程更重要"或"后台清理让路"等需求。

**核心设计决策**：
1. **默认值 10（中点值）** —— 可双向调节：`<10` 让路（如后台清理），`>10` 抢先（如关键路径）。所有现有 task 默认 10（同值），排序退化为 task_id 升序 = 现状 FIFO，**完全向后兼容**。
2. **Python API：独立 `priority` 关键字** —— `@as_task(requires=[...], priority=5)`，与 `requires` 解耦，不破坏现有 requires 的两种形式。
3. **调度语义：head-of-line skip（不阻塞）** —— 高优先级 task 若暂无可匹配 worker（如缺 capability），跳过它继续调度低优先级 task，**不饿死后面的任务**。与现有 `schedule_next` 的 first-fit 语义一致。
4. **全链路透传** —— priority 支持 worker 侧提交的 task（`TaskSubmitMessage` 跨进程携带）+ worker 崩溃恢复（`TaskMetadata` 持久化），覆盖递归任务提交场景。

**排序规则**：ready task 列表按 `(priority desc, task_id asc)` 排序。

**改动规模**：12 处插入点，全部照抄 `attribute_timeout` 的既有范式，无新机制。

---

## 1. 使用示例（新旧版本对比）

### 1.1 旧版本（现状，无 priority）

当前 `@as_task` 支持 `inputs`（数据依赖）+ `requires`（capabilities + timeout），多 task 竞争 worker 时纯 FIFO（task_id 升序），无法表达优先级：

```python
from fly import as_task

# ── 形态 1：无数据依赖 ──
@as_task()
def log_task(db, key, value):
    db.write_object(key, value)

@as_task(requires=["gpu"])                    # 带 capability，无数据依赖
def train_model(db, key, value):
    db.write_object(key, value)

@as_task(requires=(["gpu"], 2.0))             # 带 capability + timeout 降级
def backup_task(db, key, value):
    db.write_object(key, value)

# ── 形态 2：有数据依赖（静态 list）──
@as_task(inputs=lambda db, key, value: [db.get_full_name("phantom")])
def consume_phantom(db, key, value):
    db.write_object(key, value)

# ── 形态 3：有数据依赖（动态 lambda + capability 组合）──
@as_task(inputs=lambda target_db, source_db, source_key, target_key:
         [source_db.get_full_name(source_key)],
         requires=["alpha"])
def alpha_cross_db_copy(target_db, source_db, source_key, target_key):
    data = source_db.read_object(source_key)
    target_db.write_object(target_key, data)

# 多 task 同时 ready + 单 worker → 严格按提交顺序（task_id）执行
# 无法让 train_model 插队优先于 log_task 执行
```

### 1.2 新版本（加 priority 关键字）

`priority` 是 `@as_task` 的**独立关键字**，与 `inputs`/`requires` 解耦，任意组合。默认 `priority=10`：

```python
from fly import as_task

# ── 形态 1：无数据依赖 + 优先级 ──
@as_task(priority=20)                         # 高优先级：抢在普通任务前
def train_model(db, key, value):
    db.write_object(key, value)

@as_task(requires=["gpu"], priority=20)       # capability + 高优先级
def train_gpu(db, key, value):
    db.write_object(key, value)

@as_task(requires=(["gpu"], 2.0), priority=5) # timeout 降级 + 低优先级（让路）
def backup_task(db, key, value):
    db.write_object(key, value)

@as_task()                                    # 默认 priority=10，行为同现状
def log_task(db, key, value):
    db.write_object(key, value)

# ── 形态 2：有数据依赖（静态）+ 优先级 ──
@as_task(inputs=lambda db, key, value: [db.get_full_name("phantom")],
         priority=15)
def consume_phantom(db, key, value):
    db.write_object(key, value)

# ── 形态 3：有数据依赖（动态 lambda）+ capability + 优先级 三合一 ──
@as_task(inputs=lambda target_db, source_db, source_key, target_key:
         [source_db.get_full_name(source_key)],
         requires=["alpha"],
         priority=18)
def alpha_cross_db_copy(target_db, source_db, source_key, target_key):
    data = source_db.read_object(source_key)
    target_db.write_object(target_key, data)

# 单 worker 时调度顺序：train_model(20) → alpha_cross_db_copy(18, 依赖就绪后)
#                       → consume_phantom(15, 依赖就绪后) → log_task(10) → backup_task(5)
# 高优先级 task 即使后提交也插队先执行；缺 capability 时跳过它先跑能跑的（head-of-line skip，不阻塞）
```

### 1.3 典型场景速查

| 场景 | 写法 | 效果 |
|------|------|------|
| 普通任务（向后兼容） | `@as_task()` | priority=10，行为同现状 FIFO |
| 关键路径抢先 | `@as_task(priority=20)` | 优先于所有默认任务调度 |
| 后台清理让路 | `@as_task(priority=1)` | 只在没有更高优先级 task 时才调度 |
| capability + 优先级 | `@as_task(requires=["gpu"], priority=15)` | 需 gpu 且优先于默认 |
| timeout + 优先级 | `@as_task(requires=(["gpu"], 2.0), priority=8)` | 带 2 秒降级，优先级略低于默认 |
| 数据依赖 + 优先级 | `@as_task(inputs=lambda db: [...], priority=12)` | 依赖就绪后按优先级调度 |

**排序规则**：ready task 按 `(priority desc, task_id asc)` —— priority 高的先调度，同 priority 内按 task_id 升序（FIFO）。数据依赖就绪（task 进入 ready）后 priority 才生效。

---

## 2. 背景：当前调度为何是 FIFO

`TaskScheduler::schedule_next()`（`src/task/cpp/task_scheduler.cpp`）遍历 `graph_->get_ready_tasks()` 返回的 task_id 列表，**取第一个有可用 worker 的 task 调度后返回**（first-fit）。

`DependencyGraph::get_ready_tasks()`（`src/task/cpp/dependency_graph.cpp`）原实现只对 `ready_tasks_`（unordered_set）做 **`std::sort`（按 task_id 升序）**。**`get_ready_tasks` 内的 `std::sort` 是 priority 生效的唯一关键点**。把排序键从 `task_id` 换成 `(priority, task_id)`，priority 即刻生效。`schedule_next` 无需改动（first-fit 天然符合"不阻塞"语义）。

---

## 3. 实现方案（12 处插入点，全部照抄 attribute_timeout 范式）

### 3.1 C++ 核心层
1. **`src/task/cpp/dependency_graph.h`** — `TaskRequirements` 加 `int priority_ = 10;`
2. **`src/task/cpp/dependency_graph.cpp`** ★关键 — `get_ready_tasks` 的 `std::sort` 比较器改为按 `(priority desc, task_id asc)`，lambda 捕获 this 读 `task_requirements_`（mutex_ 保护下线程安全）。
3. **`src/task/cpp/task_manager.h` + `.cpp`** — `TaskMetadata` 加 `int priority_ = 10;`；`create_task` 签名加 `int priority = 10`。
4. **`src/agent/cpp/master_agent.h` + `.cpp`** — `submit_task` 签名加 `int priority = 10`；3 处构造点（提交主路径 / `create_task` 调用 / reqs 构造）。
5. **`src/agent/cpp/master_agent.cpp` 恢复路径** — 从 `TaskMetadata` 还原 priority（worker 崩溃不丢）。
6. **`src/agent/cpp/master_agent.cpp` TaskSubmitMessage handler** — 调 `submit_task` 补 `msg.priority_`。

### 3.2 跨进程消息
7. **`src/network/cpp/message_types.h`** — `TaskSubmitMessage` 加 `int priority_ = 10;`，`FLY_SERIALIZE` 列表末尾追加 `priority_`。

### 3.3 C++ export 绑定层
8. **`src/agent/export/agent_export.cpp`** — master 侧 `submit_task_with_requirements` lambda。
9. **`src/agent/export/agent_export.cpp`** — worker 侧 `submit_task` lambda。
10. **`src/agent/cpp/worker_agent.h` + `.cpp`** — `WorkerAgent::submit_task` 签名 + 消息构造。
11. **`src/task/export/task_export.cpp`** — standalone graph API `add_task_with_requirements`。

### 3.4 Python 层
12a. **`src/task/py/task.py`** — `as_task(inputs, requires, vars, priority=10)` 加独立关键字。
12b. **`src/agent/py/agent.py`** — 3 处 `submit` 加 `priority: int = 10`（抽象基类 / Master / Worker）。

---

## 4. 测试方案（TDD）

### 4.1 C++ 单测（`src/task/tests/`）
- helper `caps_priority(caps, priority)`（照抄 `caps_timeout`）。
- **`task_scheduler_test.cpp`** 3 用例：
  - `PriorityOrdersReadyTasks` — priority 10/15/20 → 调度顺序 20, 15。
  - `PriorityEqualFallsBackToTaskId` — 都 10 → task_id 升序（回归保护）。
  - `PrioritySkipDoesNotBlockLower` — 高优先级缺 capability 跳过，低优先级调度（head-of-line skip）。
- **`dependency_graph_test.cpp`** 3 用例：字段读写 + 默认值 10 + remove 后清空。

### 4.2 QA 端到端（`qa/scheduling/test_priority_scheduling.py`）
单 worker 强制串行：先提交 priority=5（低），再提交 priority=20（高），验证高优先级插队先执行（task_id=2 先于 task_id=1 完成）。不依赖 sleep 时序，零 flaky。

### 4.3 回归保护
所有现有 task 默认 10（同值），排序退化为 task_id 升序 = 现状 FIFO。现有 `ScheduleMultipleTasksFIFO` 等用例无需改动即通过。

---

## 5. 验收结果（2026-07-31）

1. ✅ `./fly.sh build //src/task/... //src/agent/... //src/network/...` 通过。
2. ✅ C++ 单测全绿（含新增 6 个 priority 用例）。
3. ✅ `./qa/runqa qa/scheduling/test_priority_scheduling.py` 通过。
4. ✅ 全量回归：52 C++ 单测 + 135 QA 零失败。
5. ✅ 现有 FIFO 单测无需改动即通过。

---

## 6. 设计权衡

### 6.1 为什么默认值是 10 而非 0
默认 0 意味着所有现有 task 处于最低优先级，用户无法设计"比普通更低"的让路任务。取中点 10 让优先级双向可调：`<10` 让路、`>10` 抢先。向后兼容性不受影响——决定兼容性的是"默认值是否全部一致"，而非值为多少（所有 task 默认 10 相同，排序退化为 FIFO）。

### 6.2 为什么 priority 是独立关键字而非塞进 requires 元组
`requires` 已有 `list[str]` 和 `tuple(caps, timeout)` 两种形式，塞进元组会让 arity 从 2 变 3，解析逻辑复杂化。独立关键字与 requires 解耦，语义清晰，可任意组合。

### 6.3 为什么用 head-of-line skip 而非严格阻塞
严格阻塞（高优先级缺 worker 时阻塞其后所有 task）语义接近"绝对优先"，但会造成资源空闲、可能饿死后台任务。head-of-line skip 与现有 first-fit 一致，高优先级缺 worker 时让能跑的先跑，零额外改动。priority 只影响"都能跑时的选择顺序"。

### 6.4 为什么不改 schedule_next
`get_ready_tasks` 返回已按 priority 排序的列表，`schedule_next` 遍历它取 first-fit。priority 排序 + 现有 first-fit = 用户裁定的"不阻塞"语义，零改动。

---

*文档制定日期：2026-07-31*
