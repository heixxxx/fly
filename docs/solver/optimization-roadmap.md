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

### 🟢 方向 1：深化现有 RAS + coarse（最匹配 fly 范式，低风险）

**理由**：RAS 的"每子域完整 solve"是粗粒度 task，与 fly 的"分布式任务"范式天然匹配，通信占比仅 ~10%（vs PCG 的 93%）。当前已收敛 11 迭代/14.4s，与 PCG+AMG 同量级。

**具体优化点**：
1. **初始化 49% 是最大块**——矩阵加载（每 worker 130MB）+ 子域 LDLT。方向：
   - 矩阵分块存储（每 worker 只读自己的行块，非全量）—— 需 solver 改造，从"每 worker 全量矩阵"变"coord 预分块发布"
   - worker 进程复用（roadmap 已记录方向）—— 消除 worker 启动 ~1.9s
2. **粗校正 IO（assemble 40ms + write 50ms）**——归约式批处理：单次 DB 操作读/写所有子域，而非循环 nsd 次
3. **调度间隙 22%**——已被 S8 优化（调度热循环），剩余是框架固有

**风险**：低。纯 solver 应用层 + 框架优化，不触碰算法正确性。

### 🟡 方向 2：给 fly 增加轻量 reduce 原语（框架层，中风险，解锁新能力）

**理由**：这是让 fly 支撑细粒度迭代法（PCG/CG）的**关键基础设施缺口**。当前跨 worker 归约 ~70ms（assemble），补齐后可降到 ~1ms。

**设计**：
- master reactor 增加一个 `REDUCE` 消息类型（控制消息，复用 master↔worker 已有 TCP 长连接，无新握手）
- worker 发 8 字节 payload（dot product 局部和）+ reduce_id
- master 累加所有 worker 的值（O(nsd) 纳秒级），通过控制消息广播结果
- **不经 pickle/DB/ObjectCache**，往返 ~1ms（单次 reactor 消息延迟）

**收益**：
- 粗校正的 assemble（读 4 子域解算 residual）可用 reduce 优化部分归约
- **解锁 fly 上实现 PCG/CG 的能力**（归约从 70ms→1ms，通信占比从 93%→~30%）
- 范数收敛判定、AMG coarse residual 等都受益

**风险**：中。触及 master agent + 消息协议，需保证不破坏现有控制消息流。但这是"加法"（新消息类型），不改现有路径。

**判断**：若未来要在 fly 上实现 PCG 等细粒度迭代法，这是**必要前置**。若只深耕 RAS，优先级低于方向 1。

### 🔴 方向 3：Chebyshev 迭代（适配 fly halo 友好特性，算法变更）

**理由**：[SC19 paper](https://sc19.supercomputing.org/proceedings/workshops/workshop_files/ws_lasalss104s2-file1.pdf) 指出 Chebyshev 迭代**无 dot product**，只需 halo exchange（fly 已支持且 ~5-10ms 可接受）。

**权衡**：
- ✅ 避免 Allreduce，完全适配 fly 的"halo 友好"特性
- ❌ 收敛慢于 PCG（需更多迭代）
- ❌ 需预估特征值范围（λ_max/λ_min），额外 setup 成本
- ❌ 算法变更，需重新验证精度/收敛性

**判断**：仅当方向 2（reduce 原语）不实施时才考虑。若 reduce 原语补齐，PCG 优于 Chebyshev。

## 五、不做的方向（明确排除）

- ❌ GPU 加速稀疏直接法（实测否决，CPU 主场）
- ❌ 引入 AmgX/Hypre/PETSc（架构范式冲突）
- ❌ 在 fly 上实现 MPI 风格树形 Allreduce（常数项差 100x，master RPC 更优）
- ❌ 全局 LU 直接法（n=1000 已 OOM，且违背迭代法可扩展性优势）

## 六、决策建议

**短期（本阶段）**：聚焦**方向 1（深化 RAS）**。具体优先：
1. 矩阵分块存储（消除每 worker 全量加载 130MB，初始化 49% 的大头）
2. 粗校正 IO 归约式批处理
3. worker 进程复用（roadmap 已记录）

**中期（若要扩展求解器算法）**：先做**方向 2（轻量 reduce 原语）**，这是解锁 PCG/CG 的基础设施。补齐后评估 fly 上 PCG vs RAS 的性能对比。

**不做**：GPU 相关（除非硬件变为多卡 + 有 MPI）、MPI 重构（除非放弃 fly 范式）。

## 七、参考文档

- [perf-n1000-optimization.md](perf-n1000-optimization.md) — S9 优化历程与数据
- [gpu-acceleration-analysis.md](gpu-acceleration-analysis.md) — GPU 直接法实测否决
- [gpu-distributed-solver-survey.md](gpu-distributed-solver-survey.md) — GPU 分布式库架构冲突
- [fly-distributed-solver-paradigm-analysis.md](fly-distributed-solver-paradigm-analysis.md) — fly 实现 PCG 的通信瓶颈
- [allreduce-log-nsd-feasibility.md](allreduce-log-nsd-feasibility.md) — Allreduce O(log nsd) 分析
- [../../docs/solver/performance.md](../../docs/solver/performance.md) — 历史 solver 性能基线
- [../../docs/matrix-solver-analysis.md](../../docs/matrix-solver-analysis.md) — RAS 算法理论
