# Solver GPU 加速可行性分析报告

> 测试日期：2026-08-04
> 环境：NVIDIA GeForce RTX 4060 Laptop 8GB（compute capability 8.9, Ada）
> 库：cupy 14.1.1 + nvidia-cusolver/12.5/12.9 runtime wheels（pip 安装，无需系统 CUDA toolkit）

## 结论：GPU 加速对当前 solver 不成立

经实测，GPU **不能加速** RAS solver 的核心计算。两个候选切入点（C++ 子域 cuSOLVER / Python 粗空间 cupy）均被数据否决。

## 实测数据

### 1. 子域稀疏 LU 分解（A 路线核心）—— GPU 全面慢于 CPU

模拟 2D Poisson 子域（与 solver nsd=4 子域同构）：

| 子域规模 | CPU scipy 分解 | GPU cupy 分解 | CPU solve | GPU solve | GPU 显存 |
|---------|---------------|--------------|----------|----------|---------|
| 4 万阶 | 116ms | 289ms (**2.5x 慢**) | 8ms | 472ms (**59x 慢**) | 44MB |
| 9 万阶 | 324ms | 431ms | 15ms | 300ms | 111MB |
| 16 万阶 | 729ms | 914ms | 27ms | 588ms | 218MB |
| **25 万阶**（子域实际规模） | 1373ms | **1649ms** | 44ms | **939ms** | 358MB |

**GPU 在稀疏 LU 分解慢 1.2-2.5x，triangular solve 慢 20-59x。**

根因（稀疏直接法的本质特性）：
1. 稀疏 LU/LDLT 的 fill-in 导致**不规则内存访问**，GPU 并行优势在稠密规则计算上，稀疏分解的列依赖链是串行的，GPU 并行度极低
2. **triangular solve（前/后代换）是严格串行递推**，GPU 完全无法并行，反受 PCIe 延迟拖累
3. cupy/cuSOLVER 的稀疏 LU 底层仍是 host 调度 + device 执行，调度开销大

### 2. n=1000 全局 LU —— GPU OOM

- n=1000 全局 LU 因子：L+U 共 1.45 亿非零，**1.08GB**（仅数据）
- cupy splu 内部还需工作空间，**8GB 显存不够**，实测 Out of memory（已用 797MB + 再申请 290MB 失败）

### 3. 稀疏 matvec —— GPU 唯一占优，但非瓶颈

| 操作 | CPU scipy | GPU cupy（稳态） | 加速 |
|------|----------|-----------------|------|
| SpMV（100万阶，5M nnz） | 4.55ms | **0.38ms** | **12x** |

GPU matvec 确实快 12x（首次 4302ms 是 JIT 编译，稳态 0.38ms）。但 solver 的 matvec（粗校正 residual `A_fine.dot(x_global)`）每 2 迭代仅 ~10ms，加速到 0.8ms 收益约 45ms（总 14.4s 的 0.3%），微不足道。

## 候选切入点否决详情

### A 路线：C++ 子域 SimplicialLDLT → cuSOLVER（已否决）
- 收益预期：初始化分解(49%) + compute solve(11%) = 60%
- **实测否决**：GPU 稀疏分解慢 1.2-2.5x、solve 慢 20-59x。投入重写（CUDA toolkit + Bazel rules_cuda + solver.h 改造）只会让性能**变差**
- 显存风险：25 万阶子域 358MB，4 子域并行超 1.4GB；n 增大时逼近 8GB 上限

### B 路线：Python 粗空间 cupy（已否决）
- 收益预期：粗 solve 仅 0.7%（10ms/次）
- **实测否决**：粗空间 splu（15625 阶）在 GPU 上 OOM 风险 + 慢；粗校正的真正瓶颈是 DB IO（assemble 40ms + write 50ms），GPU 动不了
- matvec 加速（12x）收益仅 ~45ms（0.3%）

## 为什么 GPU 在这里失效——理论支撑

GPU 加速线性代数的两个**充分条件**：
1. **计算稠密且规则**（GEMM/GEMV、FFT）—— SpMV 满足（12x 加速印证）
2. **大量独立并行任务**（batched small problems）—— solver 的子域是 4 个，并行度太低

RAS solver 的核心计算是**稀疏直接法**（LU/LDLT 分解 + triangular solve），这两者：
- 分解：有依赖链（列依赖前序列），**本质串行**，GPU 并行度低
- solve：严格串行递推，GPU 无法加速

这与 HPC 社区的共识一致：**稀疏直接法在 GPU 上收益有限甚至倒退**（参见 NVIDIA cuSOLVER 文档对稀疏 LU 的性能说明、Davis 的 Sparse Matrix Wikipedia）。GPU 的强项是稠密 LU（cuSOLVER 的 dense 显著快）和迭代法的 SpMV/预处理（AMG），而非稀疏直接分解。

## 当前 solver 真正的瓶颈（回顾）

solver n=1000 = 14.4s 分解（已优化后，详见 [perf-n1000-optimization.md](perf-n1000-optimization.md)）：
- 初始化 49%（矩阵加载 + setup，CPU 固有成本）
- 调度间隙 22%（框架固有）
- COMPUTE LDLT solve 11%（CPU 已很快，44ms/子域/迭代）
- 粗校正 6.6%（IO 为主，非计算）

**这些瓶颈都不是 GPU 能解决的**——它们要么是 IO/调度（GPU 无关），要么是 CPU 已接近最优的稀疏直接法（GPU 反而更慢）。

## 可能的 GPU 适用方向（未来，非本次）

若未来 solver 演进到以下场景，GPU 才有意义：
1. **稠密子域分解**（如边界元、谱元法）—— cuSOLVER dense 显著快
2. **AMG 预处理迭代法**（替代 RAS + 粗校正）—— SpMV/relaxation 是 GPU 强项
3. **大规模 batched 小问题**（如 nsd=100+ 的细粒度区域分解）—— GPU 批量并行
4. **更大显存的 GPU**（A100 40/80GB）—— 可放下 n=2000+ 的全局 LU

但这些是算法范式变更，超出"加速当前 solver"的范围。

## 环境（已配置，可供未来验证）

- cupy 14.1.1 + CUDA runtime wheels（cusolver/cusparse/cublas）已 pip 安装到 python3.12
- GPU 验证脚本范式见本报告（cupy sparse splu/matvec vs scipy）
- 无需系统 CUDA toolkit（pip wheel 自带 runtime）
