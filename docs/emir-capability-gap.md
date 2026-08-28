# 面向分布式 EMIR 工具的框架能力差距分析

> **定位**：框架后续演进的参考文档——盘点 Fly 当前能力现状，对照「超大规模芯片分布式 EMIR（IR Drop / 电迁移）分析工具」的目标负载，给出差距与演进方向。
> **创建**：2026-08-28，基于当时实现（commit 821854d）的代码级审计。
> **关系**：本文只做「现状 vs 目标负载」的差距判断，不复制机制细节——各机制以对应权威文档为准（链接随文给出）。演进立项时，条目应迁入 [remaining-todo.md](remaining-todo.md) / [roadmap.md](roadmap.md) 走正常决策流程。

---

## 1. 目标负载画像：EMIR 工具需要什么

| 负载特征 | 规模/形态 |
|---------|----------|
| 静态 IR Drop | 亿级节点电源网格稀疏线性系统 G·v = i，SPD（纯电阻主导），单次求解 |
| Dynamic IR | 数万时间步瞬态仿真；含封装/去耦电容后矩阵非对称甚至不定；时间步本质串行 |
| 非线性器件耦合 | 单元电流随电压变化（G(x)·x = i(x) 定点迭代 / Newton 组装），矩阵随迭代更新 |
| 电迁移（EM）后处理 | 电流密度场计算 + 逐网格寿命积分，数据吞吐大、数值密度低 |
| 输入数据 | 版图/寄生数据数十至数百 GB，由外部提取器分块产出 |
| 运行形态 | 集群（跨机）长时间运行（小时~天级），必须容错、可断点续算 |

---

## 2. 框架能力现状（与 EMIR 对口的部分）

框架已从通用 DAG 任务系统长出「分布式稀疏求解器底座」形态，以下能力**已实现并经测试验证**：

### 2.1 求解内核（范式对口）

- **RAS 域分解 + 两层 Galerkin 粗网格校正**：子域 Eigen LDLT 直接法，粗校正实测减少迭代 75–92%（见 [solver/module.md](solver/module.md)、[matrix-solver-analysis.md](matrix-solver-analysis.md)）。与电源网络分析的 domain decomposition + coarse grid 方法论同构。
- **dynamic 多时间步求解器**：三阶段架构（setup 一次缓存 LDLT/端口 → 常驻 service 线程 per 步 → controller 推进），warm start（以 sol_{t-1} 为迭代基准），残差主导收敛判据，「时间步 = 检查点边界」的重投/断点语义。见 [solver/module.md](solver/module.md)。
- **已实证边界**：big_qa 实测上限 N≈2.25M（n=1500，nnz≈11M 量级）、nsd≤8、**全部单机**（`big_qa/`）。距亿级节点差两个数量级以上，且跨机路径（TIER2 副本、net_probe）无生产数据。

### 2.2 通信与数据（长 run 底座）

- **PeerRpc worker 直连**：独立端口、可 pickle 的 PeerChannelGroup 随 task 参数传递、断连事件化、阻塞期 GIL 释放（[solver/iter-refactor-design.md](solver/iter-refactor-design.md)）；ghost/halo 交换的 outside connections 机制已内建于 solver setup。
- **存储**：db→对象两级模型、LZ4/ZSTD/ZLIB 4MB chunk 流式压缩、异步落盘 + idx 事务段回滚、两层读缓存（压缩字节/反序列化对象）、temp 对象溢出层、多副本备份、数据本地性调度。见 [storage/module.md](storage/module.md)。
- **容错编排**：断连宽限 → worker 判死 REQUEUE、failed_tasks.bin 按归属 db 持久化 + `restart_failed_tasks` 重投、写 provenance 校验。见 [architecture.md](architecture.md)。

### 2.3 运维可观测

- monitor 采集落盘（worker 负载采样、task 事件流、对象级 IO）+ Web GUI（[monitor-design.md](monitor-design.md)）；message 系统终端透出（[message-system.md](message-system.md)）。

---

## 3. 差距分析

优先级定义：**P0** = 不补就跑不起来（规模/部署硬缺口）；**P1** = 不补则精度/物理覆盖/性能不足（数值内核缺口）；**P2** = 工程化补齐。

### P0-1 跨机部署运维

- **现状**：`launch_ssh_workers`/`launch_custom_workers` 仅有接口设计未实现（roadmap F1，缺多机测试环境）；全部验证在单机多 worker。
- **差距**：EMIR 必然跨机。逃生通道已通——`fly --worker` + `expect_workers` 占位 + 外部 launcher（bsub/ssh）+ `wait_workers_registered`，需要产品化并**实测跨机路径**（TIER2 多副本、net_probe 带宽探测、跨机读延迟均无生产数据）。
- **关联**：[remaining-todo.md](remaining-todo.md) F1。

### P0-2 master 元数据索引无上限

- **现状**：master 的 `remote_idx_`/`write_provenance_` 无 LRU/TTL（[memory-growth-analysis.md](memory-growth-analysis.md) 记录触发阈值在百万对象级）。
- **差距**：亿级网格 + 分块对象化后对象数达千万级，master 内存先触顶。
- **关联**：roadmap M1/S1-3（⏸ 待触发状态需提前实施）。

### P0-3 大对象分片传输 + 背压

- **现状**：网络单帧长度为 uint32（≈4GB 硬上限），对象整取不分片，无 credit 流控；并发大读仅受连接池（`data_client_pool_size`）约束。分片+背压在 roadmap F4 被降级。
- **差距**：GB 级矩阵块/波形对象撞单帧上限；多 worker 同时拉大对象无拥塞控制。
- **建议**：对象分块读写（块级寻址）+ 简单 credit 窗口；与存储层「分块流式读」一并设计。
- **关联**：[remaining-todo.md](remaining-todo.md) F4。

### P0-4 非均匀分区输入

- **现状**：solver 分区仅支持 2D 笛卡尔均匀分块（`_factor_nsd` + BFS 重叠扩展，见 [solver/module.md](solver/module.md)）。
- **差距**：真实电源网格受 macro/PMU/bump 分布影响，均匀切块负载严重不均；不规则 power domain 无法表达。
- **建议**：分两步——① 支持用户预分区输入（子域成员列表直接注入，成本低）；② 视需要集成图划分（METIS/KaHiP）或自研几何/结构划分。

### P1-1 集合通信原语（allreduce/broadcast）

- **现状**：无 allreduce/gather 原语。现有归约路径：check 节点星形 RPC（实测每归约 ~70ms、通信占 93%）或 MapReduce 树形（经 DB 中转，延迟高）；树形归约方案已裁定否决（见 [solver/rejected-alternatives.md](solver/rejected-alternatives.md)）。
- **差距**：全局内积/范数是 Krylov 法与严格残差判据的硬前提。这也是分布式 PCG 未立项的直接原因。
- **建议**：在 PeerRpc 之上做专门的归约通道（组播 + 流水线/分层归约，面向 nsd 规模小但迭代次数多的场景调优）。

### P1-2 Krylov 内核（PCG/GMRES）

- **现状**：子域内是 LDLT 直接法 + RAS 松弛；GMRES/ORAS 向量算子代码曾存在后删除（[remaining-todo.md](remaining-todo.md) §六）；PCG 已做预研待触发。
- **差距**：dynamic IR 引入电感/电容后矩阵非对称/不定，LDLT 路径失效；SPD 假设只覆盖纯电阻 static IR。
- **建议**：前置依赖 P1-1（归约原语）；以 GMRES 优先（覆盖非对称），子域算子接口抽象化（直接法/迭代法可替换）。

### P1-3 矩阵在线更新（非线性耦合）

- **现状**：dynamic 的 `update_rhs` 回调只换右端；矩阵更新 = 换对象名 = 全新 setup（LDLT 缓存按 matrix_ref 命中）。
- **差距**：器件非线性定点迭代（G(x_k) 每轮变化）没有对应机制，无法在框架内表达 Newton/Picard 外环。
- **建议**：扩展 dynamic 三阶段为「时间步 × 非线性迭代」双环：支持子域矩阵分块原位更新 + 分解复用策略（如只重分解变化子域）。

### P1-4 多级粗网格

- **现状**：仅两层，双线性插值 + Galerkin，粗点规模有内建上限（stride 下限 2，见 [solver/module.md](solver/module.md)）。
- **差距**：亿级网格两层粗化收敛不足；AMG 库（Hypre/PETSc）集成已被否决（范式冲突，[solver/rejected-alternatives.md](solver/rejected-alternatives.md)）。
- **建议**：自研轻量多级（复用现有 Galerkin 粗化逐层套用）或粗网格分层求解；重新评估的前提是 P0-4 分区与 P1-2 内核定型。

### P1-5 数值指标监控通道

- **现状**：solver 仅经 message 系统每若干轮发一条文本日志（`SOLVER::0001`）；monitor.db 无残差/迭代字段。
- **差距**：万步瞬态 run 的收敛可观测性（残差曲线、迭代次数热图、发散预警）对调试与交付必不可少。
- **建议**：monitor 增加 solver 数值事件通道（残差快照落 monitor.db + GUI 曲线），复用现有 MONITOR 通道模式。

### P2（工程化补齐，按需触发）

| 项 | 现状与差距 | 建议 |
|----|-----------|------|
| 波形数据生命周期 | dynamic 每步 `sol_t` 持久化到 db，万步×亿节点为数百 GB，目前靠用户自管清理 | 选择性持久化（采样/热点）+ run 级自动 GC 策略 |
| 输入格式适配 | 矩阵仅自研 npz/DB 对象双模式，无 MatrixMarket 等标准读入 | 按外部提取器对接需求定；重点验证「分块喂 DB」的写吞吐 |
| 分块流式读 | 读侧对象整取（解压整对象进缓存），大对象读放大 | 与 P0-3 分片传输一体设计 |
| master 单点 | master 崩溃 = run 全废（worker 超时判失联自退）；无 HA | EDA 批处理场景先接受，靠断点重投降损；长期可与 P0-2 一并考虑 |
| 混合精度/_out-of-core | 全 double，子域须整体进内存 | 亿级单子域数百 MB 尚可；预留接口，按实测再定 |

---

## 4. 演进路径建议（依赖序）

```
P0-1 跨机部署 ──┐
P0-2 索引上限 ──┼─→ 亿级 static IR 可跑（规模基线）
P0-3 分片传输 ──┤
P0-4 非均匀分区 ─┘
        │
P1-1 归约原语 ──→ P1-2 Krylov 内核 ──→ 非对称/不定矩阵（dynamic IR 完整物理）
        │                 │
        └── P1-3 矩阵在线更新（非线性耦合，双环扩展）
                          │
P1-4 多级粗化 ←──────────┘（内核定型后再评估粗化策略）
P1-5 数值监控（随内核迭代并行补）
```

立项节奏建议：P0 四项作为「EMIR 可行性阶段」整体立项；P1-1 + P1-2 作为「物理覆盖阶段」的第一优先对；P1-3/P1-4 依赖前两者定型后启动。每项立项时迁入 [remaining-todo.md](remaining-todo.md) 并按需更新 [solver/optimization-roadmap.md](solver/optimization-roadmap.md)。

---

## 5. 维护约定

- 本文是「EMIR 差距分析」的**唯一权威落点**；其余文档引用差距条目一律链接本文。
- 差距条目被立项/实现后，在本文对应条目标注 ✅ 并链接实现文档/issue；整节完结后按[文档地图约定](README.md#文档约定)收敛（结论沉入 roadmap/architecture，本文保留作决策记录或删除）。
- 文中性能/规模数字均为写作时点的实测快照（出处见对应链接），**不作为承诺值**；复测以 qa/big_qa 最新运行为准。
