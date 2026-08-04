# 以标准分布式求解器为范式，用 fly 架构实现的操作性评估

> 评估日期：2026-08-04
> 前提：不引入 MPI/GPU 库，纯用 fly 的"分布式任务框架 + DB 通信"范式，实现标准分布式迭代求解器（矩阵按非零元行块分布到 worker，halo exchange + 全局归约）
> 方法：实测 fly 通信延迟 + 对照标准迭代法通信需求，量化操作性

## 一、核心结论

**技术上可行，但通信延迟会成为硬瓶颈，且 fly 当前架构缺少"轻量全局归约"原语。**

用 fly 实现"矩阵按行分布 + halo exchange"的迭代求解器（如分布式 CG）在编程模型上完全成立——fly 的 DB 对象天然能表达分布式向量块，task 依赖能驱动 halo exchange。**但与 MPI 直传相比，fly 的 DB 通信慢 100-1000 倍**（pickle+TCP vs device memcpy），而标准迭代法每迭代需要 halo + 全局归约，累积后通信占比会远超计算。

**关键差异**：fly 当前的 RAS solver 是"粗粒度 task"（每子域完整 LDLT solve，~90ms 计算 vs ~5-10ms 通信，通信占比 ~10%）。标准迭代法是"细粒度循环"（每迭代 SpMV ~5ms 计算 + halo + 归约），通信占比会升到 60%+。fly 的范式优势在粗粒度 task 调度，细粒度迭代下通信开销放大。

## 二、fly DB 通信实测延迟（n=1000 nsd=4，4 worker 同机）

这些是分析的基础数据（来自 `big_qa/logs/n1000_sd4_coarse` worker 日志计时）：

| 通信操作 | fly 实测 | 对应 MPI 操作 | MPI 典型 |
|---------|---------|--------------|---------|
| halo read（读 1 个邻居解向量，~6万 float=0.5MB） | **5-10ms**（含 pickle 反序列化 + ObjectCache/TCP） | `MPI_Isend/Irecv` halo | ~0.1ms |
| 全局归约 assemble（读 4 子域解拼全局） | **38ms** | `MPI_Allreduce`（标量）/ `MPI_Allgatherv`（向量） | ~0.5-2ms |
| write 解向量（~6万 float=0.5MB） | **4ms**（含 pickle + ObjectCache put） | 本地内存写 | ~0 |

**延迟差距**：halo 慢 ~50-100x，归约慢 ~20-75x。主因是 pickle 序列化 + TCP + 反序列化 + ObjectCache 管理，相对 MPI 的 device-to-device 直传。

## 三、标准分布式迭代法的通信需求（对照）

### PCG（预处理共轭梯度，SPD 矩阵最常用）

每迭代（参考 [Wikipedia CG](https://en.wikipedia.org/wiki/Conjugate_gradient_method)、[SC19 communication-avoiding CG](https://sc19.supercomputing.org/proceedings/workshops/workshop_files/ws_lasalss104s2-file1.pdf)）：

| 步骤 | 通信 | fly 成本估算 |
|------|------|------------|
| SpMV（`Ap`，分布矩阵 × 分布向量） | **halo exchange**（ghost 值，邻居解向量） | ~5-10ms |
| dot product `p·Ap`、`r·r` | **2 次全局 Allreduce**（标量） | 无原语，需单 worker 收集所有 → 38ms × 2 |
| preconditioner solve（如 Jacobi/block Jacobi） | 可能 halo（块 Jacobi）或无（Jacobi） | 0-10ms |
| 向量更新（`x+=αp`、`p=βp+r`） | 无（本地） | 0 |

**单迭代 fly 通信估算**：halo ~10ms + 2 次 Allreduce ~76ms = **~86ms**（稳态）。
**单迭代计算**：SpMV ~5ms（上次实测 CPU scipy）+ 向量更新 ~1ms = **~6ms**。
**通信占比**：86 / (86+6) = **93%**。

对比 fly 当前 RAS：计算 ~90ms / 通信 ~10ms，通信占比 ~10%。**范式翻转**——标准迭代法在 fly 上通信占 93%。

### 收敛迭代数对照

| 方法 | 典型迭代数（Poisson，AMG 预处理） | 说明 |
|------|--------------------------------|------|
| fly RAS + coarse | 9-11（实测 n=1000） | 已是迭代法，通信匹配粗粒度 task |
| PCG + AMG | ~10-20 | 同量级，但每迭代通信更频繁（dot product） |
| PCG + Jacobi | ~500-1000 | 迭代数爆炸，fly 上通信总开销不可接受 |

## 四、操作性逐项分析

### ✅ 可行且自然的部分

**矩阵分布 + 向量分块**：fly 的 DB 天然支持。把全局矩阵按行块写到 `__mat_block_{rank}`，每个 worker 读自己的块。向量分布到 `__x_{rank}_{iter}`。这比 MPI 的分布式矩阵布局更灵活（DB 对象天然命名 + 版本化）。

**halo exchange**：用 task 依赖图表达。worker A 的 SpMV task 依赖邻居 B 的 `__x_{B}_{iter-1}`（inputs 声明），master 据依赖调度。fly 的 locality scheduling（S7/A1）会优先把 task 派到持有数据的 worker。**编程模型上完全成立**。

**粗粒度 block Jacobi 预处理**：每 worker 本地解子块（与 RAS 的 LDLT solve 同构），fly 已验证可行（当前 RAS 就是这个模式）。

### ⚠️ 可行但有性能损失的部分

**稀疏 SpMV**：计算本身 ~5ms（CPU scipy 已快），但 halo 交换 ~10ms 把 SpMV 的计算优势吃掉。fly 上 SpMV 不会是瓶颈，**halo 才是**。

**预处理（如 AMG setup）**：AMG 的 coarse 网格构建需全局信息（Galerkin 粗化），fly 上需单 worker 收集全局 → 与当前 RAS 的 coarse correction 同构（已实测 ~38ms assemble）。可复用现有 `_apply_coarse_correction` 的模式。

### ❌ 缺失原语 / 硬瓶颈

**全局标量归约（Allreduce）**：PCG 每迭代 2 次 `p·Ap` / `r·r`。fly **没有轻量归约原语**——必须"所有 worker 写局部 dot → 单 worker 收集求和 → 广播结果"。这条路在 fly 上 = `write 4ms × nsd` + `assemble 38ms` + `read 4ms × nsd` ≈ **每归约 ~70ms**。PCG 每迭代 2 次 = **140ms 纯归约开销**。

这是**最致命的缺口**。MPI 的 `MPI_Allreduce` 对标量（8 字节）是 μs 级，fly 对标量也要走完整 DB 通路（pickle + TCP + cache）。

可能的缓解（但仍受限）：
- **通信避免 CG（s-step CG）**：[arXiv 2501.03743](https://arxiv.org/html/2501.03743) 每 s 步才归约一次，但增加数值稳定性要求 + 局部计算放大 s 倍
- **Chebyshev 迭代**（无 dot product，只需 halo）：[SC19 paper](https://sc19.supercomputing.org/proceedings/workshops/workshop_files/ws_lasalss104s2-file1.pdf) 避免 Allreduce，但需预知特征值范围，收敛慢于 PCG

**细粒度 task 调度开销**：fly 的 task 调度有固定开销（master 调度 + task 序列化 + worker 派发，实测每次 ~1-2ms）。标准迭代法若每 SpMV 一个 task，N 次迭代 = N × (调度 + halo + 归约)。fly 当前 RAS 把"完整子域 solve"包在一个 task（~90ms），调度开销占比 ~2%。细粒度迭代下调度开销占比上升。

## 五、量化性能预估（n=1000，假设实现分布式 PCG + block Jacobi）

假设 10 次迭代收敛（AMG 预处理），4 worker 同机：

| 阶段 | 估算 | 依据 |
|------|------|------|
| 初始化（矩阵分布 + block Jacobi setup） | ~7s | 与当前 RAS setup 同构（LDLT 分解） |
| 每迭代：SpMV + halo + 2 Allreduce + 向量更新 | ~92ms | halo 10 + Allreduce 140×... 见上 |
| 10 迭代 | ~0.9s | 92ms × 10 |
| **fly 估算总** | **~8s** | 初始化主导 |
| 对比当前 RAS（实测） | 14.4s | PCG 迭代数少但通信更频繁 |

**预估结论**：若 PCG 真能在 ~10 迭代收敛，fly 实现可能比当前 RAS 略快（迭代数少抵消通信频繁）。但这个预估**非常乐观**——假设了 AMG 预处理让 PCG 10 步收敛，而 AMG 的 fly 实现本身复杂。且每迭代 140ms 归约开销在更大规模（nsd=16+，更多 worker）会急剧恶化（assemble 读更多子域）。

**规模扩展性**：fly 通信是 O(nsd) 读，MPI Allreduce 是 O(log nsd)。nsd=16 时 fly 归约 ~280ms（读 16 块），MPI ~1ms。fly 的范式在多 worker 下退化严重。

## 六、操作性结论

| 维度 | 评估 |
|------|------|
| 编程模型 | ✅ 完全可行。DB 对象表达分布向量，task 依赖驱动 halo，天然支持 |
| 单机小规模（nsd≤8） | 🟡 可行，性能可能与当前 RAS 相当或略优（若 PCG 迭代数少） |
| 通信延迟 | ❌ halo 慢 50-100x，Allreduce 慢 20-75x（vs MPI） |
| 全局归约原语 | ❌ **缺失**。fly 无轻量 Allreduce，每归约 ~70ms 是 PCG 的致命伤 |
| 多 worker 扩展 | ❌ O(nsd) 归约 vs MPI O(log nsd)，规模扩展性差 |
| 粗粒度匹配度 | ✅ block Jacobi / 子域 solve 与 fly task 粒度匹配 |
| 细粒度匹配度 | ❌ dot product / 向量更新的细粒度通信与 fly 粗粒度 task 范式错配 |

## 七、建议

### 短期（当前架构内）
**不建议实现完整的分布式 PCG**。归约原语缺失 + 细粒度通信开销，性价比低于继续优化现有 RAS（RAS 的粗粒度 task 与 fly 范式天然匹配，通信占比仅 ~10%）。

### 若要提升分布式求解能力，更务实的方向

1. **给 fly 增加轻量归约原语**（不依赖 DB）：master 提供一个 `reduce(dot_products) -> global` 的 RPC，worker 直接发标量到 master（不经 pickle/DB），master 求和广播。这能把归约从 ~70ms 降到 ~1ms。**这是让 fly 支撑细粒度迭代法的关键基础设施**。

2. **实现 Chebyshev 迭代**（无 dot product）：避免 Allreduce，只需 halo（fly 已支持）。代价是收敛慢于 PCG + 需估特征值。适合 fly 的"halo 友好"特性。

3. **继续深化 RAS + coarse**：当前已收敛 11 迭代/14.4s，与 PCG+AMG 同量级。RAS 的"每子域一次完整 solve"是粗粒度 task，与 fly 范式最匹配。进一步优化 coarse correction 的 IO（归约式批处理）比换算法更划算。

### 中长期（若归约原语补齐后）
补齐 `reduce`/`broadcast` 轻量原语后，fly 上实现分布式 PCG 的通信占比可从 93% 降到 ~30%，变得有竞争力。但这属于**框架层增强**，非 solver 层。

## 八、参考

- [Wikipedia: Conjugate gradient method](https://en.wikipedia.org/wiki/Conjugate_gradient_method) — PCG 算法与收敛性
- [SC19: Communication Avoiding Chebyshev CG](https://sc19.supercomputing.org/proceedings/workshops/workshop_files/ws_lasalss104s2-file1.pdf) — s-step / Chebyshev 通信避免策略
- [arXiv 2501.03743: Communication-reduced CG Variants](https://arxiv.org/html/2501.03743) — 减少 halo/归约的 CG 变体
- [Dongarra: Accelerating CG with GPUs](https://www.netlib.org/utk/people/JackDongarra/PAPERS/accelerating-the-conjugate.pdf) — CG 的 SpMV/通信分析
