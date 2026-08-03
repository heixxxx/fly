# Solver n=1000 性能优化报告（S9）

> 测试日期：2026-08-04
> 测试配置：n=1000（N=1,000,000）nsd=4 omega="coarse" overlap=0.20 tol=1e-8
> 矩阵：big_qa/matrices/poisson_n1000.npz（130MB Poisson 2D）
> 测试命令：`python big_qa/test_scaling_coarse.py --only 1000 4`

## 优化历程（数据驱动）

| 版本 | solve 时间 | 阶段 | 累计提升 |
|------|-----------|------|---------|
| 优化前基线（commit `ce67cac`，S5 之前） | **27.1s** | 起点 | - |
| 框架优化后（S5+S7+S8） | 20.3s | 存储读写+锁分片+调度热循环 | -25.1% |
| + S9-1 compute coord 读一次缓存 | 18.4s | 消除每迭代大对象反序列化 | -32.1% |
| + S9-2 cfg 读一次缓存 | 17.9s | check/coarse_correction | -33.9% |
| + S9-3 assemble numpy 向量化 scatter | **14.4s** | 消除收尾 100 万次 Python 循环 | **-46.9%** |

精度全程一致：11 iter, rel_err 3.37e-12, max_err 2.68e-07, rel_res 1.28e-09。

## 热点分析与各优化点

### 优化前时间分解（27.1s）
通过 worker 日志 per-iteration 计时（`[RASG COMPUTE]`/`[RASG COARSE]` 行）分析：
- **初始化（启动→setup）≈ 8.85s（33%）**：矩阵加载 + BFS overlap 扩展 + LDLT 分解
- **迭代 ≈ 14s（52%）**：COMPUTE（每子域每迭代）+ COARSE（每 2 迭代）+ 调度间隙
- **收尾 ≈ 3.3s（12%）**：assemble solution + master 读结果

### S9-1：compute coord 读一次缓存（消除每迭代 ~130ms overhead）

**发现**：`ras_graph_compute` 每次迭代都 `db.read_object("__rasg__coord")`，coord 含 primary_sets（每子域 ~62500 节点）+ global_owner（百万节点 dict），n=1000 下 ~30-50MB pickled，反序列化 ~130ms/iter。但函数体内 coord 的 6 个字段后续**完全未使用**（实际数据全来自 setup 缓存）——纯死读取。

**优化**：首次读后缓存到进程级 cache，后续 has_cache 守卫跳过。

**效果**：COMPUTE overhead ~130ms/iter → ~9ms/iter（-93%），COMPUTE t_total 220-280ms → 89-103ms（-60%）。

### S9-2：cfg 读一次缓存

**发现**：check task（每迭代）与 _apply_coarse_correction（每 2 迭代）每次读 cfg（含 primary_sets 大对象）。

**优化**：统一首次读后缓存到 `__rasg__cfg_cache`。

**效果**：18.4s → 17.9s（-0.5s）。

### S9-3：assemble numpy 向量化 scatter（消除收尾 3.3s）

**发现**：`ras_graph_assemble` 用 Python 逐元素循环 scatter 全局解（`for pos, gidx in enumerate(...): x[gidx] = x_sd[pos]`），n=1000 下 100 万次循环。且读 cfg（大对象）。

**优化**：
- numpy 向量化：`x_global[np.asarray(primary_sets[sd_id])] = x_sd`（与 coarse correction assemble 一致）
- cfg 缓存复用
- 写 numpy array 替代 list（读端 `np.array()` 已兼容）

**效果**：17.9s → 14.4s（-3.5s，-19.6%）。收尾从 3.3s 降到 0.3s。

## 当前 14.4s 分解

| 阶段 | 耗时 | 占比 | 说明 |
|------|------|------|------|
| 初始化 | ~7.1s | 49% | 矩阵加载（130MB）+ setup（BFS+LDLT），固有成本 |
| 迭代 | ~5.7s | 40% | COMPUTE 关键路径 ~1.6s + COARSE ~0.95s + 调度间隙 ~3.1s |
| 收尾 | ~0.3s | 2% | assemble（已优化） |

## 粗校正算法优化评估（阶段 4-d）

用户关注"每次迭代收集本轮所有结果并进行粗空间求解"是否为最大瓶颈。经数据核实：

**粗校正（COARSE）实测**：5 次 × ~190ms = ~0.95s，占总 14.4s 的 **6.6%**。其中粗 solve 本身仅 10ms/次，瓶颈是 assemble（读 4 解 ~40ms）+ write（写 4 修正 ~50ms）的 IO。

**结论：粗校正不是当前最大瓶颈**。按占比排序：
1. 初始化 ~49%（矩阵加载+setup，固有成本，优化空间小）
2. 调度间隙 ~22%（task 派发/依赖解析/worker 通信，框架固有）
3. COMPUTE solve ~11%（LDLT 求解，算法核心）
4. 粗校正 ~6.6%（IO 为主，solve 本身极快）

**不建议强行优化粗校正频率/算法**：
- 减少粗校正频率（如每 3 步一次）会影响收敛性，可能增加迭代数抵消收益
- 粗校正的 IO 开销（assemble+write）已用 temp 对象（save_to_db=False，走 ObjectCache），进一步优化需跨 worker cache 共享，复杂度高收益低
- 粗 solve 本身（10ms）已极快（scipy splu，Nc=15625）

如未来初始化成本因 worker 进程复用（roadmap 优化方向）消除，粗校正占比会上升，届时再评估。

## 验证

- n=1000 nsd=4 coarse：11 iter / 14.4s / rel_err 3.37e-12（精度全程一致）
- n=1000 nsd=5 coarse：14 iter / 15.6s / rel_err 8.18e-13（不同配置稳健）
- solver QA 26/26 全绿
