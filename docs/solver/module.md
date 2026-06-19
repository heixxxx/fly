# Solver 模块 — 分布式 RAS 求解器

## 模块概述

**位置**: `src/solver/`

分布式 RAS (Restricted Additive Schwarz) 求解器，用于大规模稀疏线性系统求解。支持图重叠扩展、粗网格校正、自适应松弛因子等高级特性。

---

## 核心组件

| 组件 | 文件 | 职责 |
|------|------|------|
| RAS Graph | `py/ras_graph.py` | 图重叠 RAS 求解器（主算法） |
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
