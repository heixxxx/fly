# Task 调度热循环 micro-benchmark — 基线数据（优化前）

> 测试日期：2026-08-04
> 测试方法：`./bazel-bin/src/task/tests/scheduling_hotloop_bench`
> 优化前状态：schedule_next 循环内冗余重取 idle/ready + get_ready_tasks 每次全量 sort
> 数据来源：WSL2 单机，7 轮取中位数，纯内存调度（无网络/磁盘/Python）

## 关键发现：调度热循环呈 O(N²) 退化

**核心证据**：随着 ready task 数增加，调度吞吐**急剧下降**（非线性），这是 schedule_next 循环内每调度一个 task 都重新获取并 sort 整个 ready 集导致的 O(N² log N) 行为。

## 基线数据

### 场景 A：大批次调度吞吐（schedule_all_available 消费 N 个 ready task）

| ready task 数 | idle worker 数 | tasks/sec | 相对 50t |
|---------------|----------------|-----------|----------|
| 50 | 8 | 20219 | 1.00x |
| 200 | 16 | 3878 | **0.19x**（5.2x 退化） |
| 1000 | 32 | 575 | **0.028x**（35x 退化） |

**诊断**：典型的 O(N²) 行为。schedule_all_available 循环 N 次，每次 schedule_next 都调 get_ready_tasks（O(N log N) sort）+ get_idle_workers。N 个 task = N × O(N log N) = O(N² log N)。

### 场景 B：get_ready_tasks sort 开销（反复调，不同 ready 集大小）

| ready 集大小 | ops/sec | 相对 size=10 |
|--------------|---------|--------------|
| 10 | 342315 | 1.00x |
| 100 | 8118 | 0.024x |
| 500 | 1340 | 0.004x |
| 2000 | 275 | 0.0008x |

**诊断**：sort + 比较器内 task_requirements_ map find 的开销随集大小急剧恶化。size=2000 时仅 275 ops/sec——每次调用 3.6ms，在 attr-tick 200ms 周期下占 1.8%。

### 场景 C：反复全量调度（模拟 attr-tick 200ms 周期，每 cycle 50 task）

| 总 task 数 | tasks/sec |
|-----------|-----------|
| 10000（200 cycle × 50） | 38375 |

**诊断**：每 cycle 仅 50 task，O(N²) 不明显（50²=2500），吞吐尚可。说明小批量调度场景 H2 影响有限，但 ready 积压（场景 A）是真正的痛点。

## 优化目标

1. **H2-a 消除 schedule_next 冗余重取**：select_best_worker 内重取 idle + 重建 idle_set；schedule_next 已拿到的 ready/idle 传入复用。
2. **H2-b 消除 get_ready_tasks 每次 sort**：引入有序结构维护 ready 顺序，避免每查必排。

优化后数据将补充到本文档对比。

---

## 优化后数据（H2-a + H2-b）— 2026-08-04

### 改造方案

- **H2-a**：select_best_worker 接受 idle_workers/idle_set 参数（const 引用），由 schedule_next 一次获取后循环内复用，消除内部重取与 set 重建。零架构改动。
- **H2-b**：ready_tasks_ 从 `CMUnorderedSet<uint64_t>` 改为 `std::set<std::pair<int,uint64_t>>`，key = `{-priority, task_id}`（priority 降序、同优先级 task_id 升序 FIFO）。插入即有序，get_ready_tasks 直接遍历取 task_id，**删除 std::sort**。priority 在 task 生命周期内不可变（add_task 时确定），key 稳定。

### 优化前后对比（核心指标）

| 场景 | 规模 | 原始基线 | 优化后 | 提升 |
|------|------|---------|--------|------|
| A 大批次调度 | 50 task | 20219 | 145206 | **7.2x** |
| A 大批次调度 | 200 task | 3878 | 91739 | **23.7x** |
| A 大批次调度 | 1000 task | 575 | 30591 | **53.2x** |
| B get_ready_tasks | size=100 | 8118 | 486451 | **59.9x** |
| B get_ready_tasks | size=2000 | 275 | 23333 | **84.8x** |
| C 反复调度 | 10000 task | 38375 | 73501 | 1.9x |

### 关键结论：O(N²) 退化被彻底消除

**优化前**：场景 A 从 50t→1000t 吞吐降 35x（O(N²) 退化），B 场景 size=2000 仅 275 ops/sec。
**优化后**：场景 A 从 50t→1000t 吞吐仅降 4.7x（**接近线性，O(N²) 消除**），B 场景 size=2000 达 23333 ops/sec（85x）。

### 伸缩性曲线对比

```
场景 A (schedule_all_available 大批次调度)：
tasks/sec
  140000 │ ●优化后(145206)
  120000 │
  100000 │       ●优化后(91739)
   80000 │
   60000 │
   40000 │                    ●优化后(30591)
   20000 │
       0 │───●优化前(20219)────────────────────
            50t       200t       1000t
         优化前: O(N²) 退化(35x下降)   优化后: 近线性(4.7x下降)
```

### 维护点（H2-b 改造涉及）

ready_tasks_ 类型变更后，5 处访问点改造：
- `add_task` / `check_and_move_to_ready`：insert `{-priority, task_id}`
- `remove_task` / `mark_data_removed`：erase 需查 priority 构造完整 key（task_requirements_ 此时仍在）
- `is_task_ready` / `check_and_move_to_ready` 早期判断：查 priority 构造 key 或 find_if
- `get_ready_tasks`：遍历 set 取 `.second`，**删除 sort**

### 验证

- task unit test 全绿（含 task_scheduler_test T1-T7 全套调度测试、dependency_graph_test）
- master_agent 单测、全量 QA 139/139、stability 50/50 零 crash
