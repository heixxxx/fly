# 分布式迭代法（PCG）在 fly 上的实现路径

> 预研日期：2026-08-05
> 前提：特性 1（PeerChannelGroup）+ 特性 2（常驻 daemon task）已完整交付验证

## 一、PCG 算法概要

预处理共轭梯度法（PCG）求解 SPD 系统 Ax=b：

```
x_0 = 0; r_0 = b; p_0 = r_0
for k = 0, 1, ...
    w = A·p_k              # SpMV（分布矩阵 × 分布向量，需 halo exchange）
    α = (r_k·r_k) / (p_k·w)  # dot product（需全局归约 Allreduce）
    x_{k+1} = x_k + α·p_k
    z = M^{-1}·r_k          # 预处理（block Jacobi：本地子域求解）
    r_{k+1} = r_k - α·w
    β = (r_{k+1}·z_{k+1}) / (r_k·z_k)  # 又一个 dot product Allreduce
    p_{k+1} = z_{k+1} + β·p_k
    if ‖r_{k+1}‖ < tol: break
```

每迭代：1 次 SpMV + 1 次预处理 + 2 次 dot product Allreduce + 向量更新。

## 二、fly 上 PCG 的实现路径

### 架构（复用 v2 daemon 模式）

```
nsd 个 compute worker（各持一行块）+ 1 个 coordinator worker（归约）
各 compute worker 常驻 while task：
  while not converged:
    local SpMV + halo exchange（RPC 从邻居取 ghost 值）
    local dot product（局部和）
    RPC 发局部和给 coordinator → 等全局和返回
    向量更新
coordinator worker 常驻 while task：
  收齐 nsd 个局部和 → 求和 → 广播全局和
```

### vs RAS 的差异

| 维度 | RAS（v2 daemon） | PCG |
|------|-----------------|-----|
| 矩阵分布 | 子域 + overlap（BFS 扩展） | 行块（无 overlap，halo ghost） |
| 每迭代计算 | local LDLT solve ~90ms | local SpMV ~5ms + block Jacobi |
| 每迭代通信 | RPC 发完整子域解给 check | RPC 发 ghost 值（小）+ dot product 标量 |
| 收敛迭代数 | 9-11（coarse 加速） | 10-20（取决于预处理） |
| Allreduce 需求 | 无（RAS 无 dot product） | **每迭代 2 次**（α 和 β 的 dot product） |

### 关键：dot product Allreduce 的实现

PCG 每迭代 2 次 dot product（`(r·r)` 和 `(p·w)`），需要全局归约。fly 无 Allreduce 原语，但可用 PeerChannelGroup 的 RPC 模拟：

**方案 A：coordinator 归约**（最简，类似 v2 check）
- 每个 compute 算完局部 dot → RPC 发给 coordinator（8 字节标量）
- coordinator 收齐 nsd 个 → 求和 → RPC 回复全局和
- 延迟：~0.26ms × 2（RPC 往返）× nsd（串行收集）= ~2ms@nsd=4

**方案 B：树形归约**（对数步数，需 PeerChannelGroup worker 间互联）
- compute 间按 log₂(nsd) 步配对交换部分和
- 每步 PeerChannelGroup RPC（~0.26ms）
- 延迟：~0.26ms × log₂(nsd) = ~0.5ms@nsd=4

方案 A 对 nsd ≤ 16 足够（O(nsd) 串行收集，但每步 0.26ms）。方案 B 在 nsd ≥ 32 时有意义。

### halo exchange 的实现

PCG 的 SpMV 需要邻居的 ghost 值（矩阵块边界的行依赖邻居块的列值）。fly 实现：
- 每个行块有一个 ghost 区域（邻居块的边界行）
- 每迭代：compute_i RPC 向邻居 compute_j 请求 ghost 值（或 compute_i 主动发边界值给邻居）
- 用 PeerChannelGroup 的 worker 间互联（不仅是 compute↔coordinator，还需 compute↔compute）

**关键缺口**：当前 PeerChannelGroup 设计是星型（compute↔check）。PCG 需要全互联（compute↔compute）或邻居直连。PeerChannelGroup 的 listen/connect 是通用的，可以建立任意拓扑。需扩展为**每个 compute 有自己的 PeerChannelGroup**（listen）+ 连接邻居（connect）。

### 预处理（block Jacobi）

block Jacobi = 每个行块本地求解 M_i^{-1}·r_i。复用现有 C++ LDLT 分解（每子域一次 setup）+ solve（每迭代一次）。与 RAS 的 setup/solve 同构。

## 三、可行性评估

| 维度 | 评估 |
|------|------|
| 编程模型 | ✅ 完全可行。PeerChannelGroup + 常驻 daemon 已验证 |
| halo exchange | ✅ PeerChannelGroup 支持任意拓扑（compute 间互联） |
| dot product 归约 | ✅ 方案 A（coordinator 归约）简单有效，~2ms@nsd=4 |
| 预处理 | ✅ block Jacobi 复用现有 LDLT |
| 通信占比 | PCG 每迭代：SpMV ~5ms + halo ~1ms + 归约 ~2ms = ~3ms 通信 / ~8ms 总 = ~37%（vs RAS ~10%） |
| 收敛性 | PCG + block Jacobi 对 Poisson 需 ~50-100 迭代（不如 RAS+coarse 的 9-11） |

**关键权衡**：PCG 每迭代更快（SpMV ~5ms vs RAS LDLT solve ~90ms），但需更多迭代（50-100 vs 9-11）。对 n=1000：
- RAS v2：11 iter × ~90ms/iter = ~1s 计算 + ~7s setup = ~15.7s（实测）
- PCG 估算：50 iter × ~8ms/iter = ~0.4s 计算 + ~7s setup + ~50×3ms 归约 = ~7.6s
- PCG 可能更快（迭代快抵消多迭代），但需 AMG 预处理才能降到 ~10 迭代

## 四、实现步骤（若实施）

1. **矩阵行块分布**：coord 预分块（每 worker 一行块 + ghost 区域），类似 v2 的 coord 预构建
2. **halo 通道**：每 compute 创建 PeerChannelGroup listen + 连接邻居（compute↔compute）
3. **coordinator 归约通道**：compute↔coordinator 的 PeerChannelGroup（方案 A）
4. **PCG daemon task**：常驻 while 循环（SpMV + halo + dot 归约 + 向量更新 + 收敛）
5. **收敛判定**：coordinator 算全局 ‖r‖，广播收敛标志

## 五、结论

PCG 在 fly 上的实现路径**清晰可行**——所有基础设施（PeerChannelGroup + 常驻 daemon + RPC 归约）已就位。关键工作量在：
- 矩阵行块分布 + ghost 区域管理（数值层）
- compute↔compute halo 通道（PeerChannelGroup 扩展为全互联）
- dot product 归约（coordinator 或树形）

但对当前 Poisson 问题，RAS+coarse（9-11 迭代）已优于 PCG+block Jacobi（50-100 迭代）。PCG 的优势在需要 **AMG 预处理**时（迭代数降到 ~10），而 AMG 在 fly 上的实现复杂度高。

**建议**：当前优先深化 RAS（已收敛 11 iter/15.7s），PCG 作为备选算法保留路径文档。若未来需要不同类型的矩阵（非 SPD、非对称），PCG/GMRES 等迭代法路径已明确。
