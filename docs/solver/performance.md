# Solver 性能分析与优化记录

## 测试环境

- CPU: 4 cores
- 编译模式: O2 + FLY_RELEASE
- 测试用例: golden_n50_sd9 (n=50, 9 subdomains), golden_n500_sd4 (n=500, 4 subdomains)

---

## n=50 性能对比 (golden_n50_sd9)

### O2 + FLY_RELEASE

| 算法 | Wall Clock | 迭代次数 | t_total | read_nb | write |
|------|-----------|---------|---------|---------|-------|
| Default (omega=1.0) | 3116ms | 109 | 4.8ms | 2.4ms | 1.0ms |
| Coarse | 2481ms | 11 | 3.3ms | 0.4ms | 1.4ms |

### Baseline vs 优化后 (Default)

| 版本 | Wall Clock | t_total | read_nb | write |
|------|-----------|---------|---------|-------|
| Baseline (fastbuild) | 5578ms | 10.7ms | 6.2ms | 2.0ms |
| O2 + FLY_RELEASE | 3116ms | 4.8ms | 2.4ms | 1.0ms |
| **提升** | **-44%** | **-55%** | **-61%** | **-50%** |

---

## n=1000 性能对比

| 方法 | 时间 | 迭代 | 与 scipy 比 |
|------|------|------|------------|
| scipy SPLU (单进程直接法) | 10.6s | 1 | 1x |
| RAS coarse nsd=2 | 30.3s | 8 | 2.9x |
| RAS coarse nsd=4 | 51.6s | 10 | 4.9x |
| RAS default nsd=4 | ~120s | ~200 | ~11x |

---

## n=500 性能分析 (nsd=4 coarse)

### 时间分解

```
Wall clock: 6.41s

├─ Fly 初始化 + Worker 启动:  ~1.9s  (30%)
├─ 粗网格构建 (并行):         ~1.6s  (25%)
├─ 迭代 (step 0-8):          ~2.8s  (44%)
└─ stop:                     ~0.1s  (2%)
```

### 迭代内分解

| 阶段 | 耗时 | 说明 |
|------|------|------|
| read_nb | 1-5ms | 读取邻居数据 |
| solve | 14-21ms | 本地 LDLT 求解 |
| write | 2-4ms | 写结果 |
| coarse correction | 90-100ms | 粗网格校正 |
| 调度开销 | ~90ms | 任务分发+等待 |

### 粗网格校正分解

| 阶段 | 耗时 |
|------|------|
| assemble (读所有子域解) | 28-32ms |
| residual (残差计算) | 2-5ms |
| coarse solve (粗网格求解) | 4-8ms |
| write (写修正) | 50-80ms |

---

## Worker 数量影响 (n=1000 coarse)

| nsd | 时间 | 迭代 | 说明 |
|-----|------|------|------|
| 2 | 30.3s | 8 | 最快（通信开销小） |
| 4 | 51.6s | 10 | 适中 |
| 5 | 57.4s | 12 | 较慢 |
| 6 | 失败 | - | 4核机器不可行 |

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

## 框架开销分析

### n=500 nsd=4 coarse

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
