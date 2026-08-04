# GPU 加速分布式稀疏求解器调研 — 与 fly 架构匹配度分析

> 调研日期：2026-08-04
> 目的：评估开源 GPU 加速分布式稀疏求解器（AmgX/Hypre/PETSc/Ginkgo）能否与 fly 架构结合

## 一、候选库概览

| 库 | 维护方 | GPU 加速能力 | 分布式模型 | 接口 | License |
|----|--------|------------|-----------|------|---------|
| **[AmgX](https://github.com/NVIDIA/AMGX)** | NVIDIA | AMG（Classical/Aggregation）+ Krylov + 多种 smoother，原生 CUDA，单/多 GPU | **MPI**（multi-node，用户提供 MPI） | C API（libamgx.a/so），第三方 Python 绑定（pyamgx） | 开源（NVIDIA） |
| **[Hypre](https://github.com/hypre-space/hypre)** | LLNL/CASC | BoomerAMG（多数选项 GPU-enabled，PMIS coarsening 优化），CUDA/HIP/SYCL | **MPI**（GPU-aware/CUDA-aware MPI，1 rank/GPU） | C API，PETSc 集成 | LGPL |
| **PETSc-GPU** | Argonne | 经 Hypre/AmgX 调用 + 自带 CUDA 后端 | **MPI** | C/Fortran/Python（petsc4py） | BSD |
| **[Ginkgo](https://github.com/ginkgo-project/ginkgo)** | Aachen 等 | 跨厂商（CUDA/HIP/Intel oneAPI），AMG + 直接法 | 单机多 GPU（MPI 通过 [ginkgo-project/highfive](https://github.com/ginkgo-project/highfive)） | C++ | BSD |

## 二、关键发现：架构范式根本不匹配

**fly solver 是"分布式任务框架 + 每节点本地求解"模型**（经源码逐行核实）：
- worker = 独立 OS 子进程（`subprocess.Popen` `fly --worker`），非 MPI rank
- worker 间通信 = DataService/DataServer 三层 TCP 读 + `.dat` 文件，**无 MPI、无共享内存**
- 并行粒度 = 每子域一个粗粒度 task（含完整 LDLT setup/solve），非细粒度向量分块
- 同步 = 通过 DB 对象的依赖图（master 调度），非 `MPI_Barrier`/`Allreduce`
- 全局归约（check 粗校正）= 单进程串行 for 循环，非分布式归约

**AmgX/Hypre/PETSc 是"MPI SPMD + 细粒度向量分布"模型**：
- 一个全局矩阵分布在 MPI ranks 上，每个 rank 持有矩阵的一块 + 对应向量段
- 求解时通过 `MPI_Allreduce`/`MPI_Isend`/`MPI_Irecv` 紧耦合协作（halo exchange + 全局归约）
- 一个 `Solve()` 调用就是所有 ranks 协同，**无法切分成细粒度任务图**
- GPU 版本：1 rank 绑 1 GPU，rank 间通信走 GPU-aware MPI

**这是两种根本不同的分布式范式**，强行结合会让两套分布式机制重叠冲突。

## 三、匹配度逐项分析

### 维度 1：通信机制（最严重冲突）

| | fly | AmgX/Hypre |
|---|-----|-----------|
| worker 间数据交换 | DB 对象（pickle → ObjectCache/DataServer TCP） | MPI 消息（device buffer 直传） |
| 邻居解传递 | `db.read_object("__rasg__x_{nb}")`（序列化+网络） | MPI halo exchange（内存/PCIe，μs 级） |
| 全局归约 | 单进程串行 for 循环读所有子域 | `MPI_Allreduce`（树形聚合，ms 级） |

fly 的 DB 通信每次涉及 **pickle 序列化 + TCP + 反序列化**（n=1000 实测每次读邻居解 ~3-14ms）。AmgX/Hypre 的 MPI halo exchange 是 **device-to-device 直传**（μs 级，快 100-1000x）。若用 AmgX 替换 fly 的求解核心，fly 的 DB 通信层要么被旁路，要么成为冗余开销。

### 维度 2：并行粒度（结构性冲突）

| | fly | AmgX/Hypre |
|---|-----|-----------|
| 任务粒度 | 每子域一个 task（setup/compute/check），粗粒度 | solver 内部按 row 分块到 ranks，细粒度 |
| 调度 | master DependencyGraph + TaskScheduler | solver 内部 `MPI_Comm` 集体调用 |
| 一个"Solve" | N 个 task 的 DAG（master 调度驱动） | 单次 `Solve()` 调用（所有 ranks 同步） |

AmgX 的 `Solve()` 是黑盒——内部完成所有 ranks 的协同。无法把它拆成 fly 的"读邻居 → 本地 solve → 写解"任务图。要么 fly 的 task 框架被旁路（AmgX 全包），要么两者重叠。

### 维度 3：GPU 收益前提（与上次 GPU 调研一致）

AmgX/Hypre 的 GPU 加速来自：
1. **AMG 的 SpMV/smoothing 是 embarrassingly parallel**（GPU 强项，已实测 12x）
2. **粗网格求解用稠密 LU**（cuSOLVER dense，GPU 强项）

但 fly 的当前 solver 用的是**稀疏直接法（LDLT）**——上次实测 GPU 稀疏 LU 慢 1.2-2.5x、solve 慢 20-59x。**AmgX/Hypre 的价值在于"换用 AMG 迭代法"**，而非"加速现有 LDLT"。这是**算法范式的变更**（直接法→迭代法 + AMG 预处理），不是库替换。

### 维度 4：集成可行性

| 集成方式 | 可行性 | 问题 |
|---------|--------|------|
| **全替换**（AmgX 接管整个求解，fly 只做外壳编排） | 技术可行 | fly 的任务调度/分布式存储**全部被旁路**，退化为脚本包装。fly 的核心价值丧失。且需引入 MPI 依赖，与现有 TCP 框架并存 |
| **混合**（fly 调度 task，task 内部调 AmgX 单 GPU 求解子域） | 部分可行但收益存疑 | 单 GPU 单子域的 AMG vs 现有 LDLT：LDLT 对 30 万阶 SPD 已很快（44ms/solve），AMG setup 开销大，单子域规模下 AMG 可能更慢。且无法利用 AmgX 的多 GPU 分布式能力（fly 的 task 不跨 GPU 协同） |
| **仅用 AmgX 的 GPU AMG 替换 fly 的粗校正** | 收益极小 | 粗校正仅占 0.7%（上次实测），且 fly 粗校正的瓶颈是 DB IO 非 compute |

## 四、结论与建议

### 短期（当前 solver 范式内）：不建议集成

**根本性架构冲突**：fly 的"分布式任务 + 松耦合 DB"与 AmgX/Hypre 的"MPI SPMD + 紧耦合集体通信"是两种范式。集成会导致：
- fly 的核心价值（任务调度、分布式存储）被旁路或冗余
- 引入 MPI 依赖与现有 TCP 框架并存，增加复杂度
- GPU 收益需要算法变更（LDLT→AMG），不是库替换

且当前 solver 经 S9 优化后 n=1000 = 14.4s（vs scipy 11.5s，仅慢 25%），瓶颈是初始化/调度间隙（非计算），GPU 分布式 solver 解决不了这些。

### 中长期（若要真正用 GPU 加速）：需重构 solver 为 MPI 架构

如果 GPU 加速是核心目标，正确路径是**让 solver 本身采用 MPI SPMD 范式**（而非 fly 的 task 框架）：
- 直接用 Hypre/AmgX 的分布式矩阵布局 + GPU AMG
- 用 GPU-aware MPI 做卡间通信（halo exchange + Allreduce）
- fly 退化为"作业编排外壳"（启动 MPI job、管理生命周期），不介入数值求解

但这等于**重写 solver 的分布式层**，与 fly 当前"用分布式任务框架实现求解器"的设计哲学相悖。需评估是否值得。

### 当前环境的硬约束

- RTX 4060 Laptop **单卡** 8GB：AmgX/Hypre 的多 GPU 分布式能力用不上（只有 1 GPU）
- 无 MPI 运行时（需装 OpenMPI/MPICH）
- 无 CUDA toolkit（只有 cupy 的 runtime wheels，编译 AmgX/Hypre 需 nvcc）

单 GPU 场景下，AmgX/Hypre 的"分布式 GPU"优势完全无法发挥，退化为"单 GPU AMG"——而单 GPU AMG 的收益需实测（上次 GPU 稀疏直接法反而更慢）。

## 五、参考

- [NVIDIA/AMGX](https://github.com/NVIDIA/AMGX) — GPU AMG + MPI，C API
- [AmgX SIAM 论文](https://epubs.siam.org/doi/10.1137/140980260) — 设计与实现
- [Hypre BoomerAMG 文档](https://hypre.readthedocs.io/en/latest/solvers-boomeramg.html) — GPU-enabled 选项
- [Hypre GPUs wiki](https://github.com/hypre-space/hypre/wiki/GPUs) — 构建配置
- [Hypre 异构架构移植论文](https://www.sciencedirect.com/science/article/am/pii/S0167819121000867) — device-aware 分布式设计
- [Ginkgo 项目](https://github.com/ginkgo-project/ginkgo) — 跨厂商 GPU 线性代数
- [SciComp StackExchange: GPU 稀疏求解库对比](https://scicomp.stackexchange.com/questions/11626/gpu-accelerated-libraries-for-solving-sparse-linear-systems)
