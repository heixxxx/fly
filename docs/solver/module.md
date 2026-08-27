# Solver 模块 — 分布式 RAS 求解器

## 模块概述

**位置**: `src/solver/`

分布式 RAS (Restricted Additive Schwarz) 求解器，用于大规模稀疏线性系统求解。支持图重叠扩展、粗网格校正、自适应松弛因子等高级特性。

---

## 核心组件

| 组件 | 文件 | 职责 |
|------|------|------|
| RAS Graph | `py/ras_graph.py` | 图重叠 RAS 求解器（主算法） |
| RAS Graph Daemon | `py/ras_graph_daemon.py` | daemon 模式入口（常驻进程复用，含 `solver_openmp_threads` 配置消费） |
| Project/DB/Flows | `py/project.py` `py/dbs.py` `py/flows.py` | 项目/数据库定义与流程封装 |
| RAS Legacy | `py/ras.py` | 1D 分区 RAS 求解器（旧版） |
| SubdomainSolver | `cpp/solver.h/cpp` | C++ 子域求解器（Eigen LDLT） |
| MatrixUtils | `cpp/solver.cpp` | 矩阵构建、图扩展、子域提取 |

---

## RAS Graph 求解器

### 算法概述

RAS (Restricted Additive Schwarz) 是一种区域分解方法，将大矩阵分割为多个子域，并行求解后通过边界值交换迭代至收敛。

**核心流程**：
1. **协调阶段**：2D 分区 + BFS 重叠扩展 + 邻居关系构建
2. **迭代阶段**：并行子域求解 → 收敛检查 → 粗网格校正（可选）
3. **组装阶段**：合并子域解为全局解

### 任务拓扑

```
coord (master, 普通函数)
  │
  ├─ compute_task × nsd (worker, @as_task)
  │   每步: 读邻居值 → 本地求解 → 写结果
  │
  ├─ check_task (worker, @as_task)
  │   每步: 粗网格校正(可选) → 收敛检查 → 调度下一步
  │
  └─ assemble_task (worker, @as_task)
      最后: 合并子域解 → 写全局解
```

### 参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| nsd | - | 子域数量 |
| overlap_ratio | 0.50 | 重叠比例 |
| max_iter | 100 | 最大迭代次数 |
| tol | 1e-8 | 收敛容差 |
| omega | 1.0 | 松弛策略：1.0 / "adaptive" / "coarse" |

### Omega 策略

| 策略 | 说明 |
|------|------|
| 1.0 | 固定松弛因子 |
| "adaptive" | 自适应松弛（Aitken-like），根据误差变化调整 |
| "coarse" | 两层粗网格校正，加速收敛 |

---

## Dynamic 多右端项求解器（`ras_graph_dynamic.py`）

EmIR dynamic IR drop 场景：同一矩阵 G 连续求解 T 个时间步的 G·x_t = b_t
（b_t = f(x_{t-1})，严格串行）。API：`solve_ras_graph_dynamic(db, matrix_ref,
nsd, b0, update_rhs, num_steps, ...)`——**非阻塞 kickoff**（写 b_0 + 提交
kickoff task 后立即返回），`get_dynamic_result(db)` 按需等待整体完成。

### 架构（task 链自驱动，master 零阻塞）

```
kickoff_task (inputs=[matrix, b_0], 任意 worker)
  └─ coord 预分块写 sub_{sd} → 提交 step0 组
step t 组 = compute_dyn × nsd (requires=sd_i, prio=90) + check_dyn (requires=ras_check)
  └─ RPC 迭代到收敛 → 写 sol_t(持久)/iters_t/converged_t → 提交 controller(t)
controller_task(t) (inputs=[converged_t], 任意 worker)
  └─ 读 x_t → update_rhs 生成 b_{t+1} → 提交 step t+1 组 → 链闭合
  └─ 终止（步数用尽/回调返 None）→ 写 __rasg__dynamic_done
```

master 只做 kickoff（含 worker 池启动：nsd 个 sd_i 绑定 + 1 个 ras_check
绑定），编排全在 worker task 链上——master 上永远不做阻塞式流程。

### 关键语义

| 语义 | 实现 |
|------|------|
| task 粒度隔离 | 每时间步一组新 task（新 PeerChannelGroup）；单步迭代在长 task 内 RPC 直连（v2 思路） |
| 失败重跑 | 组失败原子传染（check 全部可观察副作用先于 respond done；compute RPC 中断一律 raise）；已完成步骤结果持久不丢，restart 只重投失败组，check 重跑后链自动恢复 |
| 冷启动安全 | db 是权威（temp 落盘恢复 + 持久对象），worker 进程缓存（LDLT 因子、粗校正 LU）纯加速——全新 run 从 db 重建 |
| worker 复用 | requires=sd_i 属性钉住 + priority=90；setup 缓存短路（gen 会话前缀，跨步命中、跨 solve 不串） |
| warm start | step t 初值取 sol_{t-1}（持久对象，restart 后仍可用）——相邻时间步解接近时迭代次数下降（QA 实测 n20 iters [9,8,8]） |

### 对象命名空间

跨步对象带 t 维度（provenance：同名不同参数重写会被拒）：
`__rasg__b_{t}`（temp，controller 逐步清理）、`__rasg__sub_{sd}`/coord/cfg/
coarse_prebuilt（temp，全程）、`__fly_chan_{group_id}`（持久，下一步
controller 删除；重投由 remove-before-write + connect 重试容错）、
`__rasg__sol_{t}`（**持久**，用户数据）、`__rasg__iters_{t}`/`converged_{t}`
（temp，controller 依赖锚点）、`__rasg__dynamic_done`（temp，用户等待点）。

限制：omega 仅支持 1.0 / "coarse"（v2 daemon 同款）；update_rhs 在 worker
上执行（cloudpickle 随 task 参数传递）；同 db 重复调用需换 sol_prefix。

---

## 粗网格校正 (Coarse Correction)

### 原理

粗网格校正是一种多重网格方法，通过在粗网格上求解残差方程来加速收敛：

1. **投影矩阵 P**：细网格 → 粗网格的双线性插值
2. **粗网格矩阵**：A_c = P^T A P (Galerkin 投影)
3. **残差计算**：r = b - A x
4. **粗网格求解**：e_c = A_c^{-1} P^T r
5. **修正**：x = x + P e_c

### 粗网格构建

```
_ensure_coarse_cached(db):
  1. 计算投影矩阵 P (双线性插值)
  2. Galerkin 投影: A_c = P^T A P
  3. LU 分解: A_c = L U
  4. 缓存到进程级 cache
```

### 预构建优化

粗网格构建从迭代循环内移到迭代前，通过 `_prebuild_coarse_grid()` 向所有 worker 分发构建任务，worker 并行构建，不阻塞 check task。

### 复杂度

| 阶段 | 复杂度 | 说明 |
|------|--------|------|
| 投影矩阵 P | O(N) | 双线性插值 |
| Galerkin 投影 | O(nnz_coarse) | 稀疏矩阵乘法 |
| LU 分解 | O(N_c^{1.5}) | 粗网格直接求解 |
| 每步校正 | O(nnz) | 残差计算 + 粗网格求解 |

---

## C++ 子域求解器

### SubdomainSolver

基于 Eigen 的 LDLT 分解求解器，用于子域局部问题求解。

**核心操作**：
- `from_coo()`: 从 COO 格式构建求解器
- `solve()`: 求解局部线性系统

### 图扩展

`ex_slv_graph_expand_overlap()`: BFS 重叠扩展，从主节点出发按图距离扩展子域边界。

### 子域提取

`ex_slv_extract_subdomain_matrix()`: 从全局矩阵提取子域局部矩阵。

---

## 性能特征

### 迭代时间分解 (n=500, nsd=4)

| 阶段 | 耗时 | 占比 |
|------|------|------|
| read_nb (读邻居) | 1-5ms | 5% |
| solve (本地求解) | 14-21ms | 20% |
| write (写结果) | 2-4ms | 3% |
| coarse correction | 90-100ms | 70% |
| 调度开销 | ~90ms | - |

### 粗网格校正分解

| 阶段 | 耗时 |
|------|------|
| assemble (读所有子域解) | 28-32ms |
| residual (残差计算) | 2-5ms |
| coarse solve (粗网格求解) | 4-8ms |
| write (写修正) | 50-80ms |

---

## 设计决策

| 决策 | 原因 |
|------|------|
| 图重叠扩展 | 自动适应非规则分区，比 1D 分区更灵活 |
| 粗网格校正 | 加速收敛，迭代次数减少 10x |
| 粗网格预构建 | 避免阻塞 check task |
| scipy 模块级 import | 避免热路径懒加载开销 |
| 进程级 cache | 粗网格 LU 分解只构建一次，所有迭代共享 |
