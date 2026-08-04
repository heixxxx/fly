# fly 能否实现 O(log nsd) Allreduce — 原理与可行性分析

> 分析日期：2026-08-04
> 问题：MPI Allreduce 如何做到 O(log nsd)？fly 能否参考实现？

## 一、MPI Allreduce 的 O(log nsd) 原理

### 核心算法：递归 halving / 蝶形（butterfly）

以 8 个 rank 计算 `sum(v_0..v_7)` 为例，**不是**"发给 root 求和再广播"（O(nsd) 串行），而是 **log₂(nsd) 步配对交换**：

```
初始：每个 rank i 持有 v_i

第1步（步长=4）：rank i ↔ rank i⊕4 交换部分和
  0↔4, 1↔5, 2↔6, 3↔7   （4 对同时进行）
  各自累加 → 持有 2 个值的和

第2步（步长=2）：rank i ↔ rank i⊕2
  0↔2, 1↔3, 4↔6, 5↔7   （4 对同时）
  → 持有 4 个值的和

第3步（步长=1）：rank i ↔ rank i⊕1
  0↔1, 2↔3, 4↔5, 6↔7   （4 对同时）
  → 每个 rank 持有全部 8 个值的和
```

### 为什么是 O(log nsd)

| 性质 | 说明 |
|------|------|
| **步数** | log₂(nsd)（8 rank = 3 步，1024 rank = 10 步） |
| **每步并行度** | nsd/2 对 rank **同时**通信（无串行瓶颈） |
| **每步延迟** | 1 次点对点往返（MPI Isend/Irecv，μs 级） |
| **总延迟** | log₂(nsd) × 单次往返延迟 |

对比"root 串行收集"（fly 当前模式 = O(nsd)）：
- root 收 nsd-1 个消息 + 求和 + 广播 → 串行瓶颈在 root
- 树形把工作量**均摊到所有 rank**，每步 nsd/2 对并行 → 并行度 nsd/2

实际 MPI 对小消息（标量）用 **Ring Allreduce**（带宽优化，延迟仍 O(log nsd) 发起 + O(nsd) 带宽均摊）或 **Rabenseifner**（reduce-scatter + allgather）。延迟复杂度量级都是 O(log nsd)。

## 二、fly 实现树形归约的可行性

### 硬约束：fly 无点对点消息原语

经源码核实（`grep` worker_send/point_to_point/allreduce/MPI 全零命中）：
- **fly worker 间无直接消息通信**，只能通过 DB 对象（DataClientPool 直连对端 DataServer 读对象）
- DataClientPool 是**短连接**（每次 request 新建 TCP + close，无 keepalive，`data_client_pool.cpp:53,86,102...`）
- 并发受 `pool_size` 信号量限制（默认 4，`config data_client_pool_size`）

### 树形归约对 fly 的两层含义

**算法层（通信拓扑）**：log₂(nsd) 步配对——这个**纯逻辑 fly 完全能表达**。每步 worker i 写部分和到 `__partial_{step}_{i}`，worker i⊕d 读它累加。task 依赖图天然驱动这个流程。

**传输层（延迟）**：这才是问题所在。树形归约的 O(log nsd) 前提是**每步配对交换是 μs 级点对点通信**。fly 的"配对交换"实际是：

```
worker i:    write_object(__partial_{step}_{i}, partial_sum)   # pickle + ObjectCache put ~4ms
worker i⊕d:  read_object(__partial_{step}_{i})                # TCP 短连接 + pickle 反序列化 ~5-10ms
             累加
```

**每步 ~9-14ms**（write + read），而 MPI 每步 ~0.1ms。log₂(nsd) 步 × 每步延迟：

| nsd | MPI（O(log nsd) × 0.1ms） | fly 树形（O(log nsd) × 12ms） | fly 当前 O(nsd)（×38ms assemble） |
|-----|--------------------------|------------------------------|----------------------------------|
| 4   | 0.2ms（2步） | 24ms | 38ms |
| 8   | 0.3ms（3步） | 36ms | 76ms |
| 16  | 0.4ms（4步） | 48ms | 152ms |
| 64  | 0.6ms（6步） | 72ms | 608ms |
| 1024| 1.0ms（10步） | 120ms | 9728ms |

### 关键洞察：fly 树形归约是"伪 O(log nsd)"

fly 树形归约的**步数**确实是 O(log nsd)，但**每步的常数项（~12ms）比 MPI（~0.1ms）大 100 倍**。在 nsd ≤ 64（fly 典型规模）时：
- 树形（48-72ms）比当前 O(nsd) 串行（152-608ms）**确实快**
- 但相对 MPI（0.4-0.6ms）仍慢 **100 倍**

**fly 树形归约降低了 asymptotic 优势但没改变绝对劣势**。对 nsd=4（当前典型），树形 24ms vs 串行 38ms，仅省 14ms，收益有限。

## 三、fly 复刻 O(log nsd) 的真正瓶颈与解法

瓶颈不在"算法拓扑"（fly 能表达树形），而在**每次配对交换的传输开销**（pickle + TCP 短连接 + ObjectCache）。要真正逼近 O(log nsd) 的延迟，必须降低单次交换的常数项：

### 方案 A：轻量 reduce RPC（不经 DB，最有效）

**给 fly 增加一个 master 侧的 `reduce` RPC 原语**：
- worker 直接发标量（8 字节）到 master 的 reactor（不经 pickle/DB）
- master 累加所有 worker 的值，广播结果（仍经控制消息，但标量极小）
- 延迟：1 次 RPC 往返（~0.5-1ms，控制消息级）× 1（master 内部 O(nsd) 求和但纳秒级）

这是 **O(1) 延迟 + O(nsd) master 串行求和**（但求和是纳秒级，非瓶颈）。对 PCG 的 dot product，每归约从 ~70ms（DB assemble）降到 ~1ms。

**代价**：仍是 master 中心化（非树形），但 master 求和标量是 O(nsd) 纳秒级，远快于 O(nsd) × 12ms 的 DB 读。**对 fly 的规模（nsd ≤ 64）这是最优解**。

### 方案 B：树形 reduce（去中心化，但常数项仍大）

worker 间按树形配对交换部分和。fly 能表达拓扑，但每次交换仍 ~12ms（DB 通路）。nsd=16 时 4 步 = 48ms，比方案 A 的 ~1ms 慢。**只有当 master 成为瓶颈（nsd 极大）时树形才有意义**，但那时 fly 的 DB 通信早已不适合。

### 方案 C：连接复用 + 二进制协议（降低常数项）

DataClientPool 改 keepalive 连接 + 标量用裸二进制（不经 pickle）。能把单次交换从 ~12ms 降到 ~1-2ms。树形 nsd=16 = 4步 × 2ms = 8ms。这是**传输层优化**，与方案 A/B 正交，可叠加。

## 四、结论

| 问题 | 答案 |
|------|------|
| MPI Allreduce 如何 O(log nsd) | 树形/蝶形配对，log₂(nsd) 步，每步 nsd/2 对并行 |
| fly 能否参考实现 | **算法拓扑能（DB+task表达树形），但传输常数项差 100x** |
| fly 树形归约的实际复杂度 | 步数 O(log nsd) 但每步 ~12ms（vs MPI 0.1ms），nsd≤64 时比当前 O(nsd) 快但仍慢 MPI 100x |
| 最优解 | **方案 A：master 侧轻量 reduce RPC**（不经 DB），~1ms/归约，对 fly 规模最优 |

**核心判断**：fly 复刻 O(log nsd) 的价值不在于"树形拓扑"（那个 fly 能做），而在于**降低单次通信的常数项**。方案 A（轻量 reduce RPC）是性价比最高的——它不追求树形（master 中心化），但把标量归约从 ~70ms 降到 ~1ms，这才是让 fly 支撑 PCG 等细粒度迭代法的关键。

树形归约在 fly 上的价值有限：nsd ≤ 64（fly 典型规模）时，master 串行求和标量是纳秒级，树形省下的"master 求和"时间可忽略，反而树形的多次 DB 交换（每步 12ms）比单次 RPC（1ms）更贵。

## 五、参考

- [Wikipedia: Allreduce / Butterfly network](https://en.wikipedia.org/wiki/Allreduce) — 树形/蝶形算法
- [MPI Forum: Collective Operations](https://www.mpi-forum.org/docs/) — Allreduce 语义
- [Ring Allreduce (Baidu)](https://andrew.gibiansky.com/) — Ring 算法带宽优化
- fly 通信实测：`docs/solver/fly-distributed-solver-paradigm-analysis.md`（halo 5-10ms，assemble 38ms）
