# Solver 备选方向预研决策集（已否决 / 待触发）

> 2026-08-16 整合：原 5 份预研文档（allreduce-log-nsd-feasibility / fly-distributed-solver-paradigm-analysis / gpu-acceleration-analysis / gpu-distributed-solver-survey / pcg-implementation-path）合并，每篇保留结论与关键依据；完整分析见 git 历史。决策中枢是 [`optimization-roadmap.md`](optimization-roadmap.md)。

---

## 1. GPU 加速稀疏直接法（实测否决）

**原 gpu-acceleration-analysis.md（2026-08-04，实测数据）**

- 子域稀疏 LU/LDLT 分解：GPU 慢 1.2-2.5x；triangular solve 慢 20-59x（25 万阶子域实测）。投入 CUDA toolkit + rules_cuda + solver 改造只会**变差**。
- n=1000 全局 LU：因子 1.08GB，8GB 显存 OOM。
- 稀疏 matvec 是 GPU 唯一占优项，但非当前瓶颈。
- 根因：稀疏直接法的 fill-in 不规则 + 串行依赖链，GPU 并行度极低——**稀疏直接法是 CPU 的主场**。

## 2. 引入 GPU 分布式求解器库（架构冲突，否决）

**原 gpu-distributed-solver-survey.md（2026-08-04）**

- AmgX / Hypre / PETSc 是 MPI SPMD + 细粒度向量分布范式，与 fly 的"分布式 task + DB 通信"根本冲突。
- 全替换 → fly 核心价值丧失；混合 → 单 GPU 单子域收益存疑（与 §1 实测一致）。
- GPU 收益需算法变更（LDLT→AMG 迭代法），非库替换能获得；当前单卡无 MPI 环境，分布式 GPU 能力用不上。

## 3. 标准分布式迭代法范式（PCG 等：可行但有硬瓶颈，待触发）

**原 fly-distributed-solver-paradigm-analysis.md（2026-08-04）**

- 编程模型可行：fly DB 对象天然表达分布式向量块，task 依赖驱动 halo exchange。
- **硬瓶颈：fly 无轻量全局归约原语**。DB 通信比 MPI 直传慢 100-1000x（pickle+TCP vs device memcpy）；PCG 每迭代 2 次 dot product 归约 ≈ 140ms，通信占比 60%+（fly 优势在粗粒度 task：当前 RAS 通信占比 ~10%）。
- 实测延迟（n=1000，4 worker 同机）：跨 worker 读固定 ~5ms（与载荷弱相关，握手+协议帧主导）vs 本地 cache 命中 5-20μs；全局 assemble ~38ms。
- 乐观预估（AMG 让 PCG 10 步收敛）下 PCG 可能略快于 RAS，但 AMG 的 fly 实现本身复杂，且归约开销随 nsd 增长恶化。

## 4. O(log nsd) Allreduce / 树形归约（伪优化，明确不做）

**原 allreduce-log-nsd-feasibility.md（2026-08-04）**

- MPI 的 O(log nsd) 来自树形/蝶形配对（每步 nsd/2 对并行、device 直传 ~0.1ms/步）。
- fly 能表达树形拓扑，但每步配对 ~12ms（DB 通路）——**步数对、常数项差 100x，是"伪 O(log nsd)"**。nsd≤64 规模下不划算。
- 若未来需要归约原语：**方案 A（master 侧轻量 reduce RPC，不经 DB）~1ms/归约，性价比最高**——已被迭代重构（PeerChannelGroup RPC 直连）部分实现。

## 5. PCG 实现路径（路径清晰，当前不做）

**原 pcg-implementation-path.md（2026-08-05）**

- 基础设施已就位：v2 daemon 模式 + PeerChannelGroup + RPC 归约可直接复用；关键工作在矩阵行块分布 + ghost 区管理 + halo 通道全互联。
- 对当前 Poisson 问题：RAS+coarse（9-11 迭代）已优于 PCG+block Jacobi（50-100 迭代）；PCG 优势需 AMG 预处理（迭代数 ~10），而 AMG 复杂度高。
- **触发条件**：需要非 SPD/非对称矩阵等 RAS 不适配的问题类型时再启用；届时 reduce 原语按 §4 方案 A 补齐。

---

## 决策汇总（详见 optimization-roadmap.md §二/§五）

| 方向 | 裁定 | 依据 |
|------|------|------|
| GPU 稀疏直接法 | ❌ 实测否决 | §1：慢 1.2-2.5x / OOM |
| AmgX/Hypre/PETSc | ❌ 架构冲突 | §2：MPI SPMD 范式不兼容 |
| MPI 风格树形 Allreduce | ❌ 明确不做 | §4：伪 O(log nsd)，常数项差 100x |
| 标准分布式 PCG | ⏸ 待触发 | §3/§5：可行但归约是硬瓶颈；RAS 当前更优 |
| Chebyshev 迭代 | ❌ 被迭代重构取代 | 无 dot product 但算法变更需重验证，且迭代重构已解决通信开销 |
