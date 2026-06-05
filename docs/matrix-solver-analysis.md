# 大规模矩阵求解方法分析：IC 电源网格 IR Drop 应用

本文档分析超大规模矩阵求解中的区域分解方法（Domain Decomposition Methods），重点评估其在 IC 电源网格 IR Drop 分析中的适用性和最优方案选择。

---

## 1. 区域分解方法概述

### 1.1 核心思想

将大矩阵分割为多个小矩阵（子域），子域边缘有部分重叠节点。通过求解每个小矩阵、交互边缘节点值、多次迭代至稳态，从而解决超大规模矩阵求解问题。

这一思想最早由德国数学家 **Hermann Amandus Schwarz** 于 **1870 年** 提出（Schwarz 交替法），后在 1980s-1990s 由 Lions、Dryja、Widlund 等人发展为现代并行计算方法。

### 1.2 算法家族

| 算法名称 | 核心特征 | 并行性 | 说明 |
|----------|---------|--------|------|
| Schwarz 交替法 (Classical) | 顺序求解子域，使用最新边界值 | 串行 | 原始方法，类似 Gauss-Seidel |
| 加性 Schwarz 法 (ASM) | 并行求解所有子域，使用上一步边界值 | 并行 | Jacobi 式并行更新 |
| 限制加性 Schwarz 法 (RAS) | ASM 改进，引入单位分解权重 | 并行 | PETSc 默认求解器，工业界最常用 |
| 乘性 Schwarz 法 (Multiplicative) | 类 Gauss-Seidel 顺序求解 | 可着色并行 | 收敛更快但并行度受限 |
| 优化 Schwarz 法 (ORAS) | 使用 Robin/阻抗边界条件 | 并行 | 适用于波动问题 |

### 1.3 数学描述

#### 问题设定

求解大规模线性系统 $A\mathbf{u} = \mathbf{b}$，其中 $A$ 为 $n \times n$ 大型稀疏矩阵。

#### 核心算子

- **限制算子** $R_i$：布尔矩阵，从全局未知量提取子域 $i$ 的局部未知量
- **局部矩阵**：$A_i = R_i A R_i^T$
- **单位分解**：对角权重矩阵 $D_i$，满足 $\sum_{i=1}^{N} R_i^T D_i R_i = I$

#### RAS 迭代公式

$$\mathbf{U}^{n+1} = \mathbf{U}^n + M_{\text{RAS}}^{-1} \mathbf{r}^n$$

其中残差 $\mathbf{r}^n = \mathbf{F} - A\mathbf{U}^n$，预条件子：

$$M_{\text{RAS}}^{-1} = \sum_{i=1}^{N} R_i^T D_i A_i^{-1} R_i$$

#### 算法伪代码

```
算法：限制加性 Schwarz (RAS)
输入：A, b, 初始猜测 U^0, 容差 ε
输出：近似解 U

1: r^0 ← b - A U^0
2: for n = 0, 1, 2, ... do
3:     for i = 1 to N (in parallel) do      // 每个子域一个处理器
4:         r_i ← R_i r^n                     // 提取子域残差
5:         δ_i ← A_i^{-1} r_i                // 求解子域局部问题
6:     end for
7:     U^{n+1} ← U^n + Σ_{i=1}^{N} R_i^T D_i δ_i   // 组装全局修正
8:     r^{n+1} ← b - A U^{n+1}
9:     if ||r^{n+1}|| / ||r^0|| < ε then return U^{n+1}
10: end for
```

### 1.4 收敛性

一维两子域情形（重叠宽度 $\delta = d - c$）的收敛因子：

$$\rho = \frac{c - a}{d - a} \cdot \frac{b - d}{b - c}$$

**重叠越大，收敛越快。** 只要子域之间有正重叠（$\delta > 0$），方法即收敛。

### 1.5 与 Block Jacobi 的关系

当重叠最小时，ASM 和 RAS 在代数层面退化为 Block Jacobi 迭代。重叠是 Schwarz 方法超越 Block Jacobi 的关键。

### 1.6 非重叠方法家族

| 方法 | 特点 |
|------|------|
| Schur 补方法 (Substructuring) | 将界面未知量与内部未知量分离 |
| Neumann-Neumann / BDD | 基于子域 Neumann 问题的对偶方法 |
| FETI | 通过 Lagrange 乘子强制界面连续性 |
| BDDC | FETI 的对偶方法，更易实现 |

---

## 2. IC 电源网格矩阵特性

### 2.1 矩阵性质

IC 电源网格 IR Drop 分析涉及求解电阻网络的大规模线性系统。其矩阵具有以下关键性质：

| 性质 | 描述 | 对求解器的影响 |
|------|------|---------------|
| **大规模** | 数百万 ~ 数亿节点（190M+ 已有报道） | 必须可扩展，直接法不可行 |
| **SPD（对称正定）** | 纯电阻网络的导纳矩阵是 SPD 的 | 可用 CG，保证收敛 |
| **SDDM（对称对角占优 M-矩阵）** | 正对角、非正非对角元素 | 支撑理论保证快速收敛 |
| **加权 Laplacian** | 本质是 3D 网格上的加权 Laplacian | 天然适合多重网格法 |
| **稀疏** | 每节点仅连少数邻居（~4-6） | 迭代法每步代价低 |
| **权重变化大** | 不同金属层电阻率差异大、via 电阻各异 | 需要鲁棒的预条件子 |
| **条件数极大** | κ(A) ≈ 10⁶ ~ 10¹⁰+ | 严重病态，高质量预条件子必不可少 |
| **解的局部性** | IR Drop 集中在高电流密度区域 | 可利用稀疏化加速 |
| **内部边界条件** | C4 bump 作为内部供电点 | 与经典 PDE 边值问题不同 |
| **拓扑半规则** | 多层网格结构，但有不规则空洞/断线 | 代数方法优于几何方法 |

### 2.2 规模量级

| 规模 | 节点数 | 电阻数 | 场景 |
|------|--------|--------|------|
| 中等规模 | 70K ~ 642K | - | 早期工业设计 |
| 大规模 | 1.6M | - | 典型 VLSI |
| 超大规模 | 60M | 100M | THUPG10 benchmark |
| 极端规模 | 190M+ | - | ASM 分布式并行记录 |

---

## 3. 求解方法对比

### 3.1 方法总览

IC 电源网格 IR Drop 求解的工业界和学术界方法分为以下几类：

#### 直接法

| 方法 | 适用规模 | 特点 |
|------|---------|------|
| SparseLU (KLU) | < 10⁶ ~ 10⁷ 节点 | 鲁棒、精确，但 O(n^1.5) 复杂度导致内存爆炸 |
| Cholesky 分解 | < 10⁶ 节点 | 适用于 SPD 矩阵，是 benchmark 标准答案 |

**优势**：鲁棒、精确。**劣势**：内存和时间不可扩展，无法处理超大规模。

#### 迭代法 + 预条件子

| 方法 | 核心思想 | 关键论文 |
|------|---------|---------|
| PCG + Incomplete Cholesky | 经典预条件 CG | Chen & Chen, DAC 2001（200× 快于 SPICE3） |
| PCG + AMG | 代数多重网格预条件 CG | Zhuo et al., IEEE TCAD 2008（1.6M 节点 DC 141s） |
| PCG + 确定性 Random Walk | 混合预条件 | DAC 2013（超越 AMG-PCG on IBM benchmarks） |
| PCG + Poisson Solver | 快速 Poisson 求解器预条件 | Yang et al., ICCAD 2011 |
| Flexible CG + Sparsification | 利用解局部性的稀疏化 CG | Freund |

#### 多重网格法

| 方法 | 特点 |
|------|------|
| 几何多重网格 (GMG) | 适用于规则网格，GPU 实现可达 25× 加速 |
| 代数多重网格 (AMG) | 纯代数，无需几何信息，工业界主流 |
| Kozhaya-style multigrid | 跳过松弛步，利用电源网格电压平滑特性 |
| PowerRush (AMG-PCG) | 聚合型 AMG + K-cycle 加速，60M 节点 170s |
| PowerRChol (2026) | 随机化 Cholesky 分解，比 AMG-PCG 快 3.64× |

#### 区域分解法

| 方法 | 关键论文 | 性能 |
|------|---------|------|
| ASM (加性 Schwarz) | Sun et al., ICCAD 2007 | 分布式并行基准 |
| Block-iterative DDM | Zhong & Wong, ISQED 2010 | 块迭代加速 |
| EPPCG (扩大分区 PCG) | Zhang & Sarin, ISQED 2014 | 61×~142× 加速 |
| ASM 并行求解 | ACM DAC 2013 | **190M 节点 < 5 分钟** |

#### 特殊方法

| 方法 | 特点 |
|------|------|
| Random Walk (Qian/Zhu/Nassif) | IC 领域独有方法，利用解的局部性，但收敛不可靠 |
| 确定性 Random Walk 预条件 | 结合 incomplete factorization + random walk 思想 |
| 支撑图预条件 (Support Graph) | 层次化支撑树构造，增量分析可达 22× 加速 |
| Machine Learning Surrogate | U-Net/GNN，30×~545× 加速，但仅适用于早期估算 |

### 3.2 性能对比

| 方法 | 已验证最大规模 | 加速比 | 复杂度 |
|------|-------------|--------|--------|
| Direct LU | ~10⁶ | 1× (基准) | O(n^1.5) |
| PCG + IC | 数百万 | 200× (vs SPICE3) | O(n·k) |
| AMG + PCG | 60M | 数百× | O(n) |
| ASM (分布式) | **190M** | 110× (vs LU) | O(n·k/N) |
| EPPCG (GPU) | 数百万 | 61×~142× | O(n·k/N) |
| GPU Multigrid PCG | 10.5M | 25× (vs CPU) | O(n) |

---

## 4. 单机最优方案：AMG-PCG

### 4.1 推荐理由

电源网格矩阵是 **SPD 加权 Laplacian**，这正是 AMG 的最优问题类型：

1. **O(n) 复杂度** — 线性可扩展到任意规模
2. **加权 Laplacian** — AMG 粗网格构造自然有效
3. **处理不规则性** — 代数方法适应不同金属层/via
4. **鲁棒性** — 对权重变化大、不规则拓扑保持快速收敛
5. **可作为预条件子** — AMG + PCG 比单独 AMG 更鲁棒

### 4.2 最佳实践

```
单机求解架构：
┌─────────────────────────────────────────┐
│  外层求解器: Preconditioned CG           │
│       ↓ 残差 → 预条件子                  │
│  预条件子: 代数多重网格 (AMG) V-cycle     │
│       ↓ 聚合 + 插值                      │
│  粗网格: 直接 Cholesky 求解              │
│                                         │
│  加速选项: GPU offload (smooth/restrict) │
│  稀疏化: 利用 IR Drop 解的局部性          │
└─────────────────────────────────────────┘
```

---

## 5. 分布式并行最优方案：Two-Level RAS + AMG

### 5.1 AMG vs DDM 通信模式对比

这是选择分布式并行方案的核心考量：

#### AMG 的通信模式

```
细网格 → 限制 → 粗网格 → ... → 最粗层
                                    ↓
                             全局直接求解 ← 通信瓶颈！
                                    ↓
细网格 ← 延拓 ← 粗网格 ← ... ← 最粗层

每层需要全局聚合(aggregation) → 全局同步
最粗层 → 所有节点参与，计算量极小但通信不变
V-cycle 每层都有同步点
```

**AMG 分布式痛点**：
- 粗网格层越深，每个处理器有效计算越少，通信占比越高
- 最粗层全局求解是串行瓶颈
- 节点数增多时，粗网格并行效率急剧下降

#### DDM (RAS) 的通信模式

```
节点1: [子域1求解] ──边界交换── [子域1求解] ──边界交换── ...
节点2: [子域2求解] ──边界交换── [子域2求解] ──边界交换── ...
节点3: [子域3求解] ──边界交换── [子域3求解] ──边界交换── ...
              ↑ 仅邻居间通信，无全局同步
```

**DDM 分布式优势**：
- 通信严格限于相邻子域（近邻通信）
- 通信量 ∝ 重叠区表面积，计算量 ∝ 子域体积
- 无全局粗网格求解瓶颈
- 天然 embarrassingly parallel（除边界交换）

#### 通信量量化对比

| 通信指标 | AMG (V-cycle) | RAS (一次迭代) |
|---------|---------------|---------------|
| 每次迭代通信轮次 | O(log n) 层 × 每层同步 | 1 轮邻居交换 |
| 全局同步次数 | O(log n) 次 Allreduce | 仅 1 次（残差检查） |
| 最粗层瓶颈 | 存在，严重 | 不存在 |
| 弱扩展性 | 受粗层限制 | 优秀（通信量恒定） |

### 5.2 电源网格拓扑优势

电源网格的 mesh 结构使 DDM 特别适合：

- **规则 mesh** → 规则分区，负载天然均衡
- **局部连通** → 每个子域只与 2-4 个邻居交换数据
- **SPD 矩阵** → RAS 收敛有保证
- **大规模** → 计算远大于通信，RAS 效率趋近 100%

### 5.3 最优架构

```
分布式并行架构：
┌──────────────────────────────────────────────────┐
│                                                  │
│  全局层: RAS (Restricted Additive Schwarz)        │
│    └─ 每个 MPI 进程负责一个子域                    │
│    └─ 迭代间仅交换重叠区边界数据 (近邻通信)         │
│    └─ 残差范数检查 (1 次 Allreduce，可异步化)      │
│                                                  │
│  局部层: 每个子域内部使用 AMG 求解                  │
│    └─ 子域内 AMG V-cycle (无通信，纯本地计算)      │
│    └─ 子域足够小时直接 Cholesky 求解               │
│                                                  │
│  可选: 全局粗空间校正 (Two-level RAS)              │
│    └─ 粗空间 = 子域中心少量代表节点                 │
│    └─ 粗问题规模很小，单节点直接求解                │
│    └─ 仅需 1 次额外 Allreduce                     │
│                                                  │
│  各取所长:                                        │
│    RAS → 解决分布式通信瓶颈（AMG 的弱点）           │
│    AMG → 解决局部求解效率（直接法在大子域上慢）      │
│    粗空间 → 保证子域数增多时收敛率不退化             │
│                                                  │
└──────────────────────────────────────────────────┘
```

### 5.4 验证数据

| 方案 | 规模 | 时间 | 来源 |
|------|------|------|------|
| ASM 并行 | 190M 节点 | < 5 分钟 | ACM DAC 2013 |
| EPPCG (MPI+GPU) | 数百万节点 | 61×~142× 加速 | Zhang & Sarin |
| GPU Multigrid PCG | 10.5M 节点 | 12 秒 | GPU-MG |

---

## 6. 推荐方案总结

### 6.1 场景选择矩阵

| 场景 | 推荐方案 | 理由 |
|------|---------|------|
| 单机 / 单节点 | AMG-PCG | O(n) 复杂度，无需通信开销 |
| 分布式并行 | **Two-Level RAS + AMG** | 近邻通信，无全局瓶颈 |
| GPU 单卡 | GPU AMG-PCG / GPU Sparse Direct | 利用 GPU 高带宽 |
| GPU 多卡 | RAS + GPU AMG 局部求解 | DDM 框架 + GPU 加速 |
| 增量分析 | 确定性 Random Walk 预条件 | 局部计算，22× 增量加速 |
| 早期估算 | ML Surrogate (GNN/CNN) | 极快但精度有限 |

### 6.2 分布式并行最终推荐

```
对于 IC 电源网格 IR Drop 的分布式并行求解：

╔══════════════════════════════════════════════════════╗
║                                                      ║
║   最优方案: Two-Level RAS + AMG                      ║
║                                                      ║
║   ┌─ 全局: RAS 迭代框架 (近邻通信，无全局瓶颈)        ║
║   ├─ 局部: AMG V-cycle 求解子域 (纯本地计算)          ║
║   └─ 粗空间: 全局粗网格校正 (保证可扩展性)            ║
║                                                      ║
║   单层 RAS:      适合 < 64 节点                       ║
║   Two-level RAS: 适合任意规模 (推荐)                   ║
║                                                      ║
║   业界记录: 190M 节点 < 5 分钟 (ASM, 2013)            ║
║                                                      ║
╚══════════════════════════════════════════════════════╝
```

---

## 7. 关键参考文献

### 区域分解方法

| # | 文献 | 说明 |
|---|------|------|
| 1 | Schwarz, H.A. (1870). *Über einen Grenzübergang durch alternierendes Verfahren* | 原始 Schwarz 交替法 |
| 2 | Lions, P.L. (1988-1990). *On the Schwarz Alternating Method I, II, III* | 现代收敛理论 |
| 3 | Dryja & Widlund (1994). *Domain Decomposition Algorithms with Small Overlap* | 小重叠算法 |
| 4 | Toselli & Widlund (2005). *Domain Decomposition Methods: Algorithms and Theory* | 教科书 |
| 5 | Dolean, Jolivet, Nataf (2015). *An Introduction to Domain Decomposition Methods* | 教科书 |

### IC 电源网格求解

| # | 文献 | 说明 |
|---|------|------|
| 6 | Chen & Chen (DAC 2001). *Preconditioned Krylov-subspace methods for power grid* | 首次 PCG 用于电源网格，200× 加速 |
| 7 | Kozhaya, Nassif, Najm (IEEE TCAD 2002). *A multigrid-like technique for power grid analysis* | 首次 multigrid 引入电源网格 |
| 8 | Su, Acar, Nassif (DAC 2003). *Power grid reduction based on AMG principles* | AMG 化简 |
| 9 | Zhuo et al. (IEEE TCAD 2008). *Power grid analysis and optimization using AMG* | 1.6M 节点 DC 141s |
| 10 | Sun et al. (ICCAD 2007). *Parallel domain decomposition for large-scale power grids* | DDM 用于电源网格 |
| 11 | ASM 并行求解 (DAC 2013). *Efficient parallel power grid analysis via ASM* | **190M 节点 < 5 分钟** |
| 12 | Zhang & Sarin (ISQED 2014). *Enlarged-partition based PCG (EPPCG)* | 61×~142× 加速 |
| 13 | Yang et al. (ICCAD 2011). *Fast Poisson solver preconditioned method* | Poisson 预条件 |
| 14 | ESPSim (TODAES 2022). *Efficient Scalable Power Grid Simulator Based on Parallel AMG* | 最新并行 AMG |
| 15 | Song et al. (DAC 2025). *Nested Domain Decomposition for Power Grid* | 最新嵌套区域分解 |

### 软件工具

| 工具 | 支持方法 |
|------|---------|
| PETSc | RAS（默认）、ASM、Block Jacobi |
| hypre | AMG、BoomerAMG |
| OpenROAD | SparseLU (Eigen) |
| PowerRush | AMG-PCG with K-cycle |
| PowerRChol (2026) | 随机化 Cholesky |
| FreeFem++ | RAS、ASM、ORAS |
