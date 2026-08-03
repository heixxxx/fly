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
