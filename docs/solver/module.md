# Solver 模块 — 分布式 RAS 求解器

## 模块概述

**位置**: `src/solver/`

分布式 RAS (Restricted Additive Schwarz) 求解器，用于大规模稀疏线性系统求解。支持图重叠扩展、粗网格校正、自适应松弛因子等高级特性。

---

## 核心组件

| 组件 | 文件 | 职责 |
|------|------|------|
| RAS Graph | `py/ras_graph.py` | 图重叠 RAS 求解器（主算法） |
| RAS Graph Daemon | `py/ras_graph_daemon.py` | daemon 模式入口（常驻进程复用，含 `solver_openmp_threads` 配置消费） |
| Project/DB/Flows | `py/project.py` `py/dbs.py` `py/flows.py` | 项目/数据库定义与流程封装 |
| RAS Legacy | `py/ras.py` | 1D 分区 RAS 求解器（旧版） |
| SubdomainSolver | `cpp/solver.h/cpp` | C++ 子域求解器（Eigen LDLT） |
| MatrixUtils | `cpp/solver.cpp` | 矩阵构建、图扩展、子域提取 |

---

## RAS Graph 求解器

### 算法概述

RAS (Restricted Additive Schwarz) 是一种区域分解方法，将大矩阵分割为多个子域，并行求解后通过边界值交换迭代至收敛。

**核心流程**：
1. **协调阶段**：2D 分区 + BFS 重叠扩展 + 邻居关系构建
2. **迭代阶段**：并行子域求解 → 收敛检查 → 粗网格校正（可选）
3. **组装阶段**：合并子域解为全局解

### 任务拓扑

```
coord (master, 普通函数)
  │
  ├─ compute_task × nsd (worker, @as_task)
  │   每步: 读邻居值 → 本地求解 → 写结果
  │
  ├─ check_task (worker, @as_task)
  │   每步: 粗网格校正(可选) → 收敛检查 → 调度下一步
  │
  └─ assemble_task (worker, @as_task)
      最后: 合并子域解 → 写全局解
```

### 参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| nsd | - | 子域数量 |
| overlap_ratio | 0.50 | 重叠比例 |
| max_iter | 100 | 最大迭代次数 |
| tol | 1e-8 | 收敛容差 |
| omega | 1.0 | 松弛策略：1.0 / "adaptive" / "coarse" |

### Omega 策略

| 策略 | 说明 |
|------|------|
| 1.0 | 固定松弛因子 |
| "adaptive" | 自适应松弛（Aitken-like），根据误差变化调整 |
| "coarse" | 两层粗网格校正，加速收敛 |

---

## Dynamic 多右端项求解器（`ras_graph_dynamic.py`）

EmIR dynamic IR drop 场景：同一矩阵 G 连续求解 T 个时间步的 G·x_t = b_t
（b_t = f(x_{t-1})，严格串行）。API：`solve_ras_graph_dynamic(db, matrix_ref,
nsd, b0, update_rhs, num_steps, ...)`——**非阻塞 kickoff**（写 b_0 + 提交
kickoff task 后立即返回），`get_dynamic_result(db)` 按需等待整体完成
（无 timeout 形参）。

### 三阶段架构（task 链自驱动，master 零阻塞）

```
kickoff_task (inputs=[matrix, b_0], 任意 worker)
  └─ coord 预分块写 sub_{sd} → 提交 setup 链

阶段 1 setup（每链一次，跨全部时间步复用）
  setup_compute × nsd (requires=worker_attr("sd_i"), inputs=[sub_{sd}])
    LDLT/子域 → 进程缓存(key 按 matrix_ref)；
    stop 旧 server → listen 新端口 → 端口入缓存(key 按 gen)；
    写地址对象 addr_{gen}_{sd}(temp，依赖锚)；set_worker_property(worker_attr("{gen}_{sd}"))
  setup_check (requires=worker_attr("check"), inputs=全部 addr + b_{start_t})
    粗校正 LU/A_fine/子域索引 → 缓存；connect × nsd → 池 {sd: conn_id}
    入缓存；被拒 = 成员 setup 后已死 → raise 下游连锁；
    完成后提交 solver(start_t) 组

阶段 2 solver per t（时间步粒度 = task 检查点边界）
  compute(t,sd) (requires=worker_attr("sd_i"), inputs=[b_t, addr])
    常驻 service 线程消费请求(agent 级队列单读者)：accept iterate(带 ghosts)
    → 本地 solve → respond 贡献；done → ack 退出。
    compute task 自身 = 参数注入者(b_local 等)，短命即回。
  check(t) (requires=worker_attr("check"), inputs=[b_t]+[sol_{t-1}])
    驱动循环：逐存活成员 call(带 ghosts) → 收贡献 → 残差主导收敛判定/
    非 coarse delta 聚合 → 粗校正；收敛广播 done → 写 sol_t(持久)/
    iters_t/converged_t → 提交 controller(t)

阶段 3 controller(t) (requires=worker_attr("check")，与 check 同 worker)
  有下一步：update_rhs(x_t, t+1) → 写 b_{t+1} → 提交 solver(t+1) → 删 b_t
  无下一步：收尾清缓存/server/属性 → 发 cleanup × nsd（各成员销毁
    listener/LDLT 缓存、关 server、移除属性、删地址对象）→ 写 dynamic_done
```

连接方向为 check→compute 主动连接：成员"该连未连"的时序洞不存在——
setup 失败走依赖连锁，运行期死亡由断连事件毫秒级传播。

动态链 RPC 传输契约（2026-09-01 起）：双方向已切 PeerRpc 流式管线
（89b37b5——check 请求经 peer_stream_writer、member 响应经
peer_stream_respond_writer，压缩块流承载，大 ghost/解向量无整体缓冲）；
f64 载荷（ghost 贡献/解向量近随机）显式 compression="none"（5c2f127——
lz4 对近随机数据压缩必失败，85% 规则虽转 raw 直通，但尝试 CPU 在压缩
线程=关键路径上，纯负优化）。NOT_READY 跨步旧参数校验（4d27164）：serve
侧请求与 step_ctx 各带 t 严格比对，t 不匹配（check(t) 早于 compute(t)
注入到达的竞态）同回 NOT_READY——check 下一收集圈重试，不再有用 t-1
旧参数应答的窗口。

master 只做 kickoff（含 worker 池：总数不足先补空属性进程，再 ensure_workers
按 `db.worker_attr`（rasg:{uid}: 命名空间，见下）申请属性编队），编排全在
worker task 链上——master 上永远不做阻塞式流程。

### 编队属性命名（issue 009 收紧）

worker 属性与 task requires 统一经 `db.worker_attr(tag)` = `rasg:{uid}:{tag}`
（单点定义在 SolveDb；uid 跨进程持久于 _DB_META）。并发求解 flow 各持不同
uid → 属性零交集，调度精确匹配不串池；申请编队时 `exclude=r"^rasg:"` 排除
已被其他 flow 占用的 worker。restart 闭环：load_db 回来的同 db uid 相同，
bin 还原的 requires 与重新 ensure 分配的属性自动一致。

### 关键语义

| 语义 | 实现 |
|------|------|
| task 粒度隔离 | 时间步 = task 组边界 = 天然检查点：组失败原子传染，已完成步骤结果持久不丢 |
| 常驻 service 线程 | agent 级 PeerRpc 请求队列全生命周期只能有一个 reader——短命 task 各自 recv 会互抢错账（实测事故）；唯一消费者是 setup 启动的 daemon 线程，solver task 只注入本步参数 |
| 双向死亡感知 | 成员死/task 异常退出：except 强关 server → FIN → check 的 call 立即 FAILED → alive<nsd → 组死 raise → stop_peer_rpc → 全体成员挂起 recv 被断连唤醒——无任何等待窗口 |
| 冷启动安全 | db 是权威（temp 落盘恢复 + 持久对象），进程缓存纯加速；缓存 key 分层：数据按 matrix_ref（重投不重做 LDLT）、连接按 gen（换代重建，隔离重投窗口） |
| remove-before-write | check 写 sol_t 系、controller 写 b_{t+1} 前 try remove——重投链换 gen 后参数 hash 不同，不清会被 provenance/DUPLICATE 拒绝 |
| 重投重启 | compute 缓存 miss → no-op return（真组由新链发出）；check 池 miss → 以新 gen 从 setup 重新驱动该时间步（LDLT 命中秒过）；旧 gen task 被换代 stop 唤醒退出，最坏一轮涟漪后二次重投全 no-op 收敛 |
| warm start | step t 迭代基准取 sol_{t-1}（持久对象，restart 后仍可用）——n500 coarse 实测稀疏右端项 iters [5,4,4]（-20%）、均匀小变化 [7,5,5]（-29%@tol=1e-8） |
| 收敛判定 | coarse 模式**残差主导**：r_rel = ‖b_t−A·x‖/‖b_t‖ < tol 即收敛（step≥min_steps 防呆）——数学准则直接界定解误差；非 coarse 用成员 delta 标志聚合。tol=1e-8 实测 rel_res≤7.5e-9 全达标、rel_err ~1e-11 |
| timeout 策略 | **数据规模相关等待一律无超时**（用户裁定）：rpc/call/recv 显式 timeout_ms=0（C++ 绑定层 gil_scoped_release 释放 GIL 后阻塞，断连事件唤醒）、controller 链推进、get_dynamic_result 全部无限等待。失败语义全部事件化：PeerRpc 断连 → rpc FAILED / recv RuntimeError；wait_obj can_still_produce 兜底生产者死绝。关机逃生口 = agent.is_running() |

### 对象命名空间与缓存 key

跨步对象带 t 维度（provenance：同名不同参数重写会被拒；全部写前 remove）：
`__rasg__b_{t}`（temp，controller 逐步清理）、`__rasg__d_addr_{gen}_{sd}`
（temp，setup 写 = 依赖锚 + 地址载体，cleanup 删）、sub_{sd}/coord/cfg/
（temp，kickoff 写全程存活）、`__rasg__coarse_static`/`__rasg__coarse_ac`
（temp，coord 预构建 T3 双对象拆分，77ee15b：static 存只读数据 P 数组/
N/Nc/b/stride，读走默认 low 等级；ac 存 splu 消费会原地重排的 Ac 数组，
消费方显式 cache="none"——每次全新反序列化，无污染面）、
`__rasg__sol_{t}`（**持久**，用户数据）、`iters_{t}`/`converged_{t}`
（temp，controller 锚点）、`__rasg__dynamic_done`（temp，用户等待点）。

进程缓存 key 两层：数据按 `matrix_ref`（LDLT/粗校正，同矩阵跨代跨 solve
共享）；连接按 `gen`（listener 端口、channel 池，换代即重建隔离重投窗口）。

限制：omega 仅支持 1.0 / "coarse"；update_rhs 在 worker 上执行（cloudpickle
传递）且须**确定性**（重投会重调）；同 db 重复调用需换 sol_prefix。

restart 属性编队已由 ensure_workers 根治（2026-08-27，原已知限制见
`docs/issues/009-dynamic-restart-worker-pool-contract.md`）：run2 流程为
load_db → 按编队规模缺口补空属性进程 → `ensure_workers(attrs,
exclude=r"^rasg:")`——bin 还原的 requires 与本次申请同源于 worker_attr
自动闭环，与 launch/load_db 顺序无关。

---

## 粗网格校正 (Coarse Correction)

### 原理

粗网格校正是一种多重网格方法，通过在粗网格上求解残差方程来加速收敛：

1. **投影矩阵 P**：细网格 → 粗网格的双线性插值
2. **粗网格矩阵**：A_c = P^T A P (Galerkin 投影)
3. **残差计算**：r = b - A x
4. **粗网格求解**：e_c = A_c^{-1} P^T r
5. **修正**：x = x + P e_c

### 粗网格构建

```
_ensure_coarse_cached(db):
  1. 计算投影矩阵 P (双线性插值)
  2. Galerkin 投影: A_c = P^T A P
  3. LU 分解: A_c = L U
  4. 缓存到进程级 cache
```

### 预构建优化

粗网格构建从迭代循环内移到迭代前，通过 `_prebuild_coarse_grid()` 向所有 worker 分发构建任务，worker 并行构建，不阻塞 check task。

### 复杂度

| 阶段 | 复杂度 | 说明 |
|------|--------|------|
| 投影矩阵 P | O(N) | 双线性插值 |
| Galerkin 投影 | O(nnz_coarse) | 稀疏矩阵乘法 |
| LU 分解 | O(N_c^{1.5}) | 粗网格直接求解 |
| 每步校正 | O(nnz) | 残差计算 + 粗网格求解 |

---

## C++ 子域求解器

### SubdomainSolver

基于 Eigen 的 LDLT 分解求解器，用于子域局部问题求解。

**核心操作**：
- `from_coo()`: 从 COO 格式构建求解器
- `solve()`: 求解局部线性系统

### 图扩展

`ex_slv_graph_expand_overlap()`: BFS 重叠扩展，从主节点出发按图距离扩展子域边界。

### 子域提取

`ex_slv_extract_subdomain_matrix()`: 从全局矩阵提取子域局部矩阵。

---

## 性能特征

### 迭代时间分解 (n=500, nsd=4)

| 阶段 | 耗时 | 占比 |
|------|------|------|
| read_nb (读邻居) | 1-5ms | 5% |
| solve (本地求解) | 14-21ms | 20% |
| write (写结果) | 2-4ms | 3% |
| coarse correction | 90-100ms | 70% |
| 调度开销 | ~90ms | - |

### 粗网格校正分解

| 阶段 | 耗时 |
|------|------|
| assemble (读所有子域解) | 28-32ms |
| residual (残差计算) | 2-5ms |
| coarse solve (粗网格求解) | 4-8ms |
| write (写修正) | 50-80ms |

---

## 设计决策

| 决策 | 原因 |
|------|------|
| 图重叠扩展 | 自动适应非规则分区，比 1D 分区更灵活 |
| 粗网格校正 | 加速收敛，迭代次数减少 10x |
| 粗网格预构建 | 避免阻塞 check task |
| scipy 模块级 import | 避免热路径懒加载开销 |
| 进程级 cache | 粗网格 LU 分解只构建一次，所有迭代共享 |
