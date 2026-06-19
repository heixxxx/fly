# Solver 性能分析与优化记录

## 测试环境

- CPU: 4 cores
- 编译模式: O2 + FLY_RELEASE
- 测试用例: qa/solver/ 下的 golden 测试

---

## scipy SPLU 基准测试

| n | N | nnz | 时间 | 精度 |
|---|---|-----|------|------|
| 20 | 400 | 1,920 | 0.001s | 机器精度 |
| 50 | 2,500 | 12,300 | 0.004s | 机器精度 |
| 500 | 250,000 | 1,248,000 | 1.30s | 机器精度 |

---

## Fly RAS 求解器性能

### n=50 (N=2,500)

| 算法 | nsd | 迭代 | Wall Clock | 与 scipy 比 | t_total | read_nb | write | solve |
|------|-----|------|-----------|------------|---------|---------|-------|-------|
| Default | 9 | 110 | 3.6s | 900x | 4.3ms | 2.1ms | 0.8ms | 0.0ms |
| Coarse | 9 | 11 | 2.5s | 625x | 3.6ms | 0.4ms | 1.1ms | 0.0ms |

### n=500 (N=250,000)

| 算法 | nsd | 迭代 | Wall Clock | 与 scipy 比 |
|------|-----|------|-----------|------------|
| Coarse | 4 | 9 | 6.3s | 4.9x |

#### Baseline vs 优化后（排除矩阵生成）

| 版本 | Run 1 | Run 2 | Run 3 | 平均 | 提升 |
|------|-------|-------|-------|------|------|
| Baseline (O2) | 8.04s | 8.12s | 8.04s | 8.07s | - |
| Optimized (O2+FLY_RELEASE) | 6.29s | 6.32s | 6.42s | 6.34s | **-21.3%** |

优化来源：
- FLY_RELEASE + INFO→DBG 日志消除：~1.0s
- 粗网格预构建：~0.5s
- 依赖位置预取 + TaskManager 优化：~0.2s

#### 每迭代 timing (coarse)

| 阶段 | 耗时 |
|------|------|
| compute (read_nb + solve + write) | 82.4ms |
| coarse correction | 102.2ms |
| **总计** | **~185ms** |

#### compute 阶段分解

| 阶段 | 耗时 | 说明 |
|------|------|------|
| read_nb | 2.0ms | 读取邻居数据 |
| solve | 18.4ms | 本地 LDLT 求解 |
| write | 2.9ms | 写结果 |

#### coarse correction 分解

| 阶段 | 耗时 | 说明 |
|------|------|------|
| assemble | 29.2ms | 读取所有子域解 |
| residual | 2.0ms | 残差计算 |
| coarse solve | 5.0ms | 粗网格求解 |
| write | 61.5ms | 写修正结果 |

### n=1000 (N=1,000,000)

| 算法 | nsd | 迭代 | Wall Clock | 与 scipy 比 |
|------|-----|------|-----------|------------|
| Coarse | 2 | 8 | 30.3s | 2.9x |
| Coarse | 4 | 10 | 51.6s | 4.9x |

---

## 时间分解 (n=500, nsd=4, coarse)

```
Wall clock: 6.41s

├─ Fly 初始化 + Worker 启动:  ~1.9s  (30%)
├─ 粗网格构建 (并行):         ~1.6s  (25%)
├─ 迭代 (step 0-8):          ~2.8s  (44%)
└─ stop:                     ~0.1s  (2%)
```

---

## 优化历史

### 1. INFO→DBG 日志修复

热路径中的 INFO 日志改为 DBG，消除每 worker ~3400 条日志 I/O 开销。

- `data_service.cpp`: TIER1/TIER2/TIER3 INFO→DBG
- `data_server.cpp`: DS-ACCEPT/DS-Q/DS-SEND INFO→DBG
- `master_agent.cpp`: WriteRegister INFO→DBG
- `dependency_graph.cpp`: [DEP] INFO→DBG

**效果**: read_nb -61%

### 2. FLY_RELEASE 编译 flag

DBG 宏在 release 模式下编译为空宏，彻底消除热路径日志开销。

配置：`./fly.sh build --config=opt`

### 3. 依赖位置预取

TaskAssignMessage 携带依赖数据位置，worker 读取时直接使用预取位置，避免查询 Master。

**效果**: read_nb -35%

### 4. sendv 合并发送

DataServer 使用 writev 将 header 和 payload 合并为一次系统调用。

**效果**: write -8%

### 5. TaskManager 按状态分桶

任务元数据按状态分桶存储，按状态查询从 O(n) 降到 O(k)。

**效果**: write -5%

### 6. DependencyGraph 反向索引

mark_data_ready 从 O(P×D) 降到 O(T×D)。

### 7. scipy 模块级 import

避免热路径懒加载，worker 启动时即完成 import。

### 8. 粗网格预构建

粗网格构建从迭代循环内移到迭代前，worker 并行构建。

**效果**: 迭代时间 -51%

### 9. stop() 流程重构

三阶段流程：等待任务完成 → 发送 shutdown → 等待 worker 断开。

---

## 框架开销分析 (n=500, nsd=4)

| 阶段 | 耗时 | 占比 |
|------|------|------|
| Fly 初始化 + Worker 启动 | 1.9s | 30% |
| 粗网格构建 (并行) | 1.6s | 25% |
| 迭代计算 | 2.8s | 44% |
| stop | 0.1s | 2% |

### 调度开销分解

```
check 完成 → 下一个 compute 开始: ~169ms
  ├─ check task 完成通知 master: ~10ms
  ├─ master 分发 compute tasks: ~50ms
  ├─ worker 接收 task: ~10ms
  └─ worker 开始执行: ~100ms (Python 启动开销)
```

---

## 瓶颈分析

### 当前瓶颈

1. **框架开销** (30%): Worker 启动、任务调度、stop 流程
2. **粗网格构建** (25%): LU 分解 O(N_c^{1.5})
3. **迭代计算** (44%): 实际求解 + 粗网格校正

### 优化方向

| 方向 | 潜在收益 | 难度 |
|------|---------|------|
| Worker 进程复用 | -1.9s (30%) | 高 |
| 粗网格增量更新 | -1.6s (25%) | 高 |
| 批量任务调度 | -0.7s (11%) | 中 |
| 减少 Python 开销 | -0.3s (5%) | 高 |
