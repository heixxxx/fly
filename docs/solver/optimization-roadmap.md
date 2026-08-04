# Solver 优化方向决策（基于多轮调研与实测）

> 更新日期：2026-08-04
> 依据：S9 性能优化（27.1→14.4s）+ GPU 加速实测否决 + 分布式求解器范式分析 + Allreduce O(log nsd) 分析 + 本地/跨机延迟实测

## 一、已完成的优化与现状

### S9 性能优化（已落地，commit `9625672`/`ec20313`/`de2522b`）
n=1000 nsd=4 coarse：**27.1s → 14.4s（-47%）**，精度全程一致（11 iter, rel_err 3.37e-12）。
- 框架优化（S5/S7/S8）：27.1→20.3s（存储读写 + 锁分片 + 调度热循环）
- solver 应用层（S9-1/2/3）：20.3→14.4s（coord/cfg 缓存 + assemble 向量化）

### 当前 14.4s 时间分解
| 阶段 | 耗时 | 占比 | 性质 |
|------|------|------|------|
| 初始化（矩阵加载+setup LDLT） | ~7.1s | 49% | CPU 固有成本（每 worker 加载全量矩阵 + 子域分解） |
| 调度间隙（task 派发/依赖/通信） | ~3.1s | 22% | 框架固有 |
| COMPUTE solve（每迭代 LDLT solve） | ~1.6s | 11% | CPU 已接近最优 |
| COARSE 粗校正（每 2 迭代） | ~0.95s | 6.6% | IO 为主（assemble 40ms + write 50ms），solve 仅 10ms |
| 收尾 assemble | ~0.3s | 2% | 已向量化 |

## 二、已否决的方向（不重复投入）

### ❌ GPU 加速稀疏直接法（实测否决）
详见 [gpu-acceleration-analysis.md](gpu-acceleration-analysis.md)。
- GPU 稀疏 LU 分解慢 1.2-2.5x、triangular solve 慢 20-59x（25 万阶子域实测）
- n=1000 全局 LU 因子 1.08GB，8GB 显存 OOM
- 根因：稀疏直接法的 fill-in 不规则 + 串行依赖链，GPU 并行度极低
- **稀疏直接法是 CPU 的主场，GPU 无优势**

### ❌ 引入 GPU 分布式求解器库（架构冲突）
详见 [gpu-distributed-solver-survey.md](gpu-distributed-solver-survey.md)。
- AmgX/Hypre/PETSc 是 MPI SPMD + 细粒度向量分布，与 fly 的"分布式任务 + DB 通信"范式根本冲突
- 全替换→fly 核心价值丧失；混合→单 GPU 单子域收益存疑
- GPU 收益需算法变更（LDLT→AMG 迭代法），非库替换
- 当前单卡 + 无 MPI 环境，分布式 GPU 能力用不上

### ⚠️ fly 实现标准分布式 PCG（可行但有硬瓶颈）
详见 [fly-distributed-solver-paradigm-analysis.md](fly-distributed-solver-paradigm-analysis.md)。
- 编程模型可行（DB 表达分布向量，task 驱动 halo）
- **致命缺口：fly 无轻量 Allreduce 原语**，每归约 ~70ms（跨 worker assemble）
- PCG 每迭代 2 次 dot product 归约，通信占 93%

## 三、关键实测发现（更新认知）

### 本地 cache vs 跨 worker 延迟（修正前期判断）
实测（n=1000，4 worker 同机）：

| 操作 | 本地 ObjectCache 命中 | 跨 worker（DataClientPool TCP） |
|------|---------------------|------------------------------|
| 读 8 字节标量 | **5μs** | ~5ms（pickle+TCP 握手+协议） |
| 读 1KB | 20μs | ~5ms（固定开销主导，与数据量弱相关） |
| 读 500KB | 201μs | ~5-10ms |
| 全局 assemble（读 4 子域） | — | ~38ms |

**洞察**：跨 worker 通信的延迟由 **pickle 头 + TCP 握手 + DataServer 协议帧** 的固定开销主导，与有效载荷（8 字节 vs 500KB）弱相关。8 字节的 dot product 也要付 ~5ms，因为被 pickle 包装成 ~60 字节 + TCP 短连接握手。

### Allreduce O(log nsd) 的真相
详见 [allreduce-log-nsd-feasibility.md](allreduce-log-nsd-feasibility.md)。
- MPI 的 O(log nsd) 来自树形/蝶形配对，每步 nsd/2 对并行
- fly 能表达树形拓扑，但**每步配对 ~12ms**（DB 通路）vs MPI ~0.1ms（device 直传），常数项差 100x
- fly 树形是"伪 O(log nsd)"——步数对但绝对延迟仍慢 MPI 100x
- **对 fly 规模（nsd≤64），master 侧轻量 reduce RPC 比树形更优**

## 四、更新的优化方向（按性价比排序）

### 🟡 方向 1：深化现有 RAS + coarse（边际收益小，S9 已摘完低垂果实）

**实测评估**（setup 7.1s 分解，n=1000 nsd=4 单子域）：

| 步骤 | 耗时 | 可优化性 |
|------|------|---------|
| 矩阵 np.load + tolist | ~130ms | ❌ 已很快（原假设"分块存储省 130MB"实测只值 130ms，不值得改造） |
| adjacency 构建（argsort） | ~85ms | ❌ 已 cache 共享 |
| **BFS overlap 扩展** | **~633ms** | ⚠️ Python set 循环，C++ 化可加速但工作量大 |
| rank filter | ~52ms | ❌ 已很快 |
| **LDLT 分解（Eigen -O2）** | **~1171ms** | ❌ 算法固有，C++ 已优化 |

**结论**：初始化的大头是 **BFS（633ms）+ LDLT 分解（1171ms）**，都是算法固有 CPU 计算，非框架/IO 问题。矩阵分块存储（原计划）实测只值 ~130ms，性价比低。**S9 已把框架层面的低垂果实摘完**。

剩余可考虑的点（收益有限）：
- worker 进程复用（消除 worker 启动 ~1.9s）—— 框架层，roadmap 已记录
- BFS C++ 化（633ms→~50ms？）—— 工作量大，收益不确定
- 粗校正 IO 归约批处理（assemble 40ms + write 50ms）—— 收益 ~90ms/次

**风险/收益比已不划算**。真正的提升空间转向迭代重构（方向 2）。

### 🟢 方向 2：迭代重构 — 常驻 task + 轻量 RPC（确认方向，已有完整设计）

这是当前**真正的提升空间**。消除调度间隙（3.1s，22%）+ DB 数据交换开销，预估 14.4s → ~10s（-30%）。

**完整设计**详见 [iter-refactor-design.md](iter-refactor-design.md)。核心两个特性：
1. **轻量 RPC 接口（PeerChannelGroup）**：可 pickle 的 channel 工厂随 task 参数传递，listen/connect 内置 DB 地址交换，RPC 式通信，三层故障检测（主动通知 > 断连 > 超时）+ 2 次重连
2. **迭代重构**：nsd+1 个常驻 while task（compute RPC 直连 check），消除每轮 task 调度 + DB read/write

**实施顺序**：特性 1（RPC 接口）独立交付验证 → 特性 2（迭代重构）基于特性 1。

### 🔴 方向 3：Chebyshev / reduce 原语（已被方向 2 取代，不再单独考虑）

迭代重构（方向 2）用 PeerChannelGroup + 常驻 task 直接解决了通信开销问题。reduce 原语和 Chebyshev 迭代是之前的备选方案，现已被更优的迭代重构设计取代。

### 🔴 方向 3（旧）：Chebyshev 迭代

~~[SC19 paper](https://sc19.supercomputing.org/proceedings/workshops/workshop_files/ws_lasalss104s2-file1.pdf) 指出 Chebyshev 迭代**无 dot product**，只需 halo exchange（fly 已支持且 ~5-10ms 可接受）。~~
- ❌ 算法变更，需重新验证精度/收敛性

**判断**：仅当方向 2（reduce 原语）不实施时才考虑。若 reduce 原语补齐，PCG 优于 Chebyshev。

## 五、不做的方向（明确排除）

- ❌ GPU 加速稀疏直接法（实测否决，CPU 主场）
- ❌ 引入 AmgX/Hypre/PETSc（架构范式冲突）
- ❌ 在 fly 上实现 MPI 风格树形 Allreduce（常数项差 100x，master RPC 更优）
- ❌ 全局 LU 直接法（n=1000 已 OOM，且违背迭代法可扩展性优势）

## 六、决策建议（更新）

**本阶段**：方向 1（深化 RAS）实测边际收益小（S9 已摘完框架低垂果实，剩余是 BFS/LDLT 算法固有成本）。**直接转向方向 2（迭代重构）**——这是当前唯一有实质提升空间（-30%）的方向。

**实施路径**（方向 2，已有完整设计 [iter-refactor-design.md](iter-refactor-design.md)）：
1. 特性 1：轻量 RPC 接口（PeerChannelGroup）—— 独立交付，验证 RPC 延迟
2. 特性 2：迭代重构（常驻 task）—— 基于特性 1

**不做**：GPU 相关（除非硬件变为多卡 + 有 MPI）、MPI 重构（除非放弃 fly 范式）、矩阵分块存储（实测只值 130ms）。

## 七、参考文档

- [iter-refactor-design.md](iter-refactor-design.md) — **迭代重构完整设计（当前优先方向）**
- [perf-n1000-optimization.md](perf-n1000-optimization.md) — S9 优化历程与数据
- [gpu-acceleration-analysis.md](gpu-acceleration-analysis.md) — GPU 直接法实测否决
- [gpu-distributed-solver-survey.md](gpu-distributed-solver-survey.md) — GPU 分布式库架构冲突
- [fly-distributed-solver-paradigm-analysis.md](fly-distributed-solver-paradigm-analysis.md) — fly 实现 PCG 的通信瓶颈
- [allreduce-log-nsd-feasibility.md](allreduce-log-nsd-feasibility.md) — Allreduce O(log nsd) 分析
