# Fly 与主流分布式任务框架对比调研

> 调研日期: 2026-06-25（2026-06-25 重写，整合四轮定位修正）
> 注: 文中提及的 `ARCHITECTURE_REVIEW.md` 已于 2026-08-16 文档重组时删除（结论沉淀于 roadmap/ISSUES，git 历史可查）；§1.3 部分状态过期，以 roadmap §一 的重核实为准。本文保留作战略定位参考。
> 目的: (1) 核对 fly 文档与实现的对齐度，盘点设计中尚未落地的功能；
>      (2) 调研 GitHub 高 star 分布式任务执行框架，对比异同，总结 fly 的优势与劣势；
>      (3) 基于 fly 的真实定位，提炼可被 fly 借鉴的优点与功能；
>      (4) 给出 fly 的适用领域边界。
> 数据来源: 各框架 GitHub 仓库（star 数为调研当日实测）、官方文档、pracdata《State of Workflow Orchestration 2025》、Ansys SeaScape 公开技术资料。

---

## 第一部分：Fly 文档与实现对齐度

### 1.1 结论：对齐度高，文档可信

通读 `CLAUDE.md` / `docs/architecture.md` / `docs/ARCHITECTURE_REVIEW.md` / `docs/ISSUES.md` 及各模块 `module.md`，并抽查关键源码（`master_agent.cpp` / `task_scheduler.*` / `worker_agent.*` / `database.cpp`）后：

- **架构分层、模块职责、消息类型、线程模型** 等描述与实现一致。
- `ARCHITECTURE_REVIEW.md` 列出的并发/网络/性能问题**带有明确状态标记**（`[已修复]` / `[经验证无风险]` / `[待修复]`），状态更新及时，未发现"声称已修但代码未改"的脱节。
- `ISSUES.md` 38 项中 24 项 FIXED、4 项 PENDING、9 项 RECORD ONLY，账目清晰。

### 1.2 文档声称"已完成"且经源码核实 ✅

| 设计功能 | 核实位置 |
|----------|----------|
| 六层架构 + BUILD 级无循环依赖 | `src/*/cpp/BUILD` deps 链 |
| ObjectCache 两层 LRU（low=压缩字节 / high=反序列化对象） | `src/storage/cpp/object_cache.h` |
| DataServer epoll + send_thread_pool | `src/storage/cpp/data_server.*` |
| 两段式 DataResponseProtocol（避免大 payload 用户态拷贝） | `src/network/cpp/message_protocol.h` |
| DependencyGraph 反向索引（mark_data_ready O(T×D)） | `src/task/cpp/dependency_graph.*` |
| 失败任务持久化 + `restart_failed_tasks` | `master_agent.cpp` persist_pending_tasks |
| load_db 按 hostname 分配 worker | `master_agent.cpp` + `IdxLoadCommand` |
| 动态 Worker 属性 + capability 匹配 + 超时降级 | `task_scheduler.*` attr_timeout |
| 失败 task 脏数据清理（事务化段标记 BEGIN/END/ABORT + abort truncate） | `local_index.*` |

### 1.3 文档声称"尚未实现"，经源码核实确为空缺 ⏳

| 设计功能 | 文档声明 | 源码核实 |
|----------|----------|----------|
| **SSH Worker 启动** (`launch_ssh_workers`) | ⏳ 接口设计完成未实现 | `grep launch_ssh_workers src/` → 无任何实现 |
| **自定义 Worker 启动** (`launch_custom_workers`) | ⏳ 接口设计完成未实现 | 同上，无实现 |
| **Locality 调度**（数据位置感知任务分配） | ⏳ 未实现 | `task_scheduler.cpp:91 set_locality_preference()` 是空函数体（`/*enabled*/`） |
| **Worker role 调度**（hybrid / storage_only 差异化分配） | ⏳ 未实现 | 调度器未读取 role |
| **Database Freeze 后处理**（idx 合并 / merged.idx / _META 聚合） | ⏳ 未实现 | master 仅广播 freeze 通知；无 IdxRequest 收集合并逻辑（消息类型已定义但 master 无 handler） |
| **Worker 失败恢复**（in-progress task 状态恢复） | ⏳ 未实现 | `on_disconnect` 把 RUNNING→PENDING 重新入队，但无 worker 失败时的精确状态恢复 |

### 1.4 已记录但未在"实现状态"章节列出的工程债

来自 `ARCHITECTURE_REVIEW.md`，与后续竞品对比相关：

- **Reactor 单线程 + 同步 handler**：`HandlerThreadPool` 已定义但未启用（3.1），耗时 handler 阻塞整个 reactor。
- **send 阻塞**：reactor 线程 `send()` 在 EAGAIN 下 `poll(POLLOUT, 30000)` 最长阻塞 30 秒（3.6）。
- **无背压/流控**：Worker→Master 无 credit-based 流控（5.3）；DataResponse 大对象无分片（5.4）。
- **协议无版本号**：MessageHeader 无 version 字段（3.7）。

---

## 第二部分：高 Star 分布式任务执行框架调研

按品类划分为两组：**分布式执行引擎**（与 fly 同类，重点对比）和 **工作流编排器**（不同品类，借鉴参考）。

### 2.1 框架全景与 star 数（2026-06-25 实测）

| 框架 | Stars | 语言 | 品类 | 核心定位 |
|------|------:|------|------|----------|
| **Apache Airflow** | 45,910 | Python | 编排器 | DAG 工作流调度（批处理/ETL） |
| **Ray** | 43,002 | Python/C++ | 执行引擎 | 统一 AI/Python 分布式计算 |
| **Celery** | 28,625 | Python | 任务队列 | 异步任务队列（broker/backend） |
| **Prefect** | 22,678 | Python | 编排器 | Pythonic 工作流编排 + 状态管理 |
| **Temporal** | 21,210 | Go | 持久化执行 | Durable Execution（容错工作流） |
| **Luigi** | 18,747 | Python | 编排器 | Spotify 管道（老牌，活跃度下降） |
| **Dagster** | 15,742 | Python | 编排器 | 资产/数据感知编排 |
| **Dask** | 13,854 | Python | 执行引擎 | 并行计算 + task graph 调度 |
| **Joblib** | 4,369 | Python | 本地并行 | 单机/轻量并行（Loky 后端） |

> star 数来自 `api.github.com/repos/<repo>` 当日返回的 `stargazers_count`。

### 2.2 重点框架架构速写（与 fly 强相关）

**Ray（执行引擎，与 fly 最可比）** — 三大原语 `tasks`（无状态远程函数）/`actors`（有状态工作进程）/`objects`（ObjectRef 引用，存于 Plasma 对象存储）；head 节点 GCS（全局元数据 + actor 生命周期）+ **每节点本地 scheduler**；Plasma 对象自动迁移/溢写；ObjectRef lineage 容错（对象丢失按依赖链重算）。

**Dask（执行引擎，科学计算导向）** — scheduler + workers + client；scheduler 维护中央 task graph，按依赖就绪 + 数据 locality + worker 负载调度；内置状态机精细管理 task/worker preference；与 NumPy/Pandas 同构 API。

**Celery（任务队列）** — broker（Redis/RabbitMQ）+ backend（结果存储）+ worker pool 解耦；可靠性靠 `acks_late`（完成后才 ack）+ 重试策略；不维护 task 依赖图，不调度数据传输——纯消息分发。

**Temporal（持久化执行）** — workflow 函数被框架持久化重放（event sourcing），activity（实际工作单元）由独立 worker 执行；内置 retry policy（指数退避）、多层级 timeout、**Saga 补偿模式**；状态存数据库，worker 无状态可水平扩展。

**Airflow / Prefect / Dagster / Luigi（编排器）** — 共性：DAG/Flow 定义、定时调度、UI 监控、状态持久化、重试。它们**不自己执行分布式计算**，而是把 task 派发给执行器（CeleryExecutor / KubernetesExecutor / DaskExecutor / Ray executor）。编排器与执行引擎是分层关系。

---

## 第三部分：Fly 的定位（经过四轮修正的精确版）

> 本部分是全文的轴心。fly 的"定位"不是一次性结论，而是经过四轮讨论逐步逼近的：① 数值求解框架（过窄）→ ② 通用大文件流水线（过宽）→ ③ 以数据形态为核心判据（精确）。下文是最终结论。

### 3.1 fly 的本质：类型化分布式对象存储驱动的任务框架

剥离跑在 fly 之上的应用（mapreduce / solver 都只是使用 fly 的应用尝试，**不是 fly 本身**），fly 提供的**框架原语**只有四样：

| 原语 | API | 本质 |
|---|---|---|
| **类型化分布式对象存储** | `open_db` / `write_object` / `read_object` / `freeze` / `load_db` | 数据是**命名、持久、可跨会话复用**的内存对象 |
| **声明数据依赖的任务** | `@as_task(inputs=...)` | 任务是"对数据的变换"，输入依赖显式静态声明 |
| **数据依赖驱动的调度** | DependencyGraph | 数据就绪 → 任务就绪，而非手工编排控制流 |
| **Worker 间数据直传** | DataServer + remote_idx | 数据就近访问，不经 master 中转 |

### 3.2 与主流框架的根本差异：数据媒介的形态

fly 区别于所有主流框架的根本点，在于**任务间协作的数据媒介形态**：

| 框架 | 协作媒介 | 与 fly 的差异 |
|---|---|---|
| **fly** | **类型化命名对象**（序列化往返的内存对象，持久、可复用） | 独有 |
| Ray | ObjectRef（内存对象引用，引用消失即 GC） | fly 数据持久，引用消失仍在 |
| Celery | 消息（broker 投递，无状态） | fly 数据是一等公民，任务是变换器 |
| Airflow | 控制流 DAG（XCom 传少量值） | fly 数据即依赖，调度由数据驱动 |
| Dask | task graph（延迟求值的函数链） | fly 数据立即持久化，可断点续跑、可复用 |

### 3.3 核心约束：fly db 只适合"类型化数值对象"，不是文件系统

这是定位 fly 适用领域的**最关键判据**。fly db 的全部核心价值建立在"序列化往返能还原成内存对象"这一前提上：

| fly db 能力 | 依赖的前提 |
|---|---|
| ObjectCache high 层（反序列化对象缓存） | fly 知道怎么把字节变回 `T` |
| 零拷贝 FlyBufferPtr 共享 | 对象是 fly 原生缓冲区格式 |
| bitsery 版本化 / pickle 类型化 | 数据带类型，可还原 |
| 压缩省 IO | 数据是**可压缩的原始数值**，不是已压缩格式 |

**一旦数据是 fly 无法反序列化的标准文件格式（mp4/jpg/png/pdf/zip/wav），这四项能力全部失效**：
- high 层缓存无法命中（fly 不知如何把 mp4 字节变成"视频对象"）；
- 压缩不仅无效反而浪费 CPU（mp4/jpg 已高压缩，再 LZ4 是负优化）；
- 序列化优势完全用不上，db 沦为**低效块存储**——还不如直接用共享文件系统。

**因此 fly db 存储的是"应用层定义的、有类型结构的领域对象"，本质是一个类型化分布式对象存储（typed object store），不是 blob store / 文件系统。**

### 3.4 适用领域的三步判别法

判断一个领域是否适合 fly，按顺序三问，**任一为否即不适合**：

1. **数据是 fly 可序列化的类型化内存对象吗？**（否 → 不适合。如 mp4/pdf/FASTQ 标准格式）
2. **数据是可压缩的原始数值表示吗？**（否 → 不适合。如已压缩的图片/音视频）
3. **数据需跨阶段持久化流转、被 fly 任务直接消费吗？**（否 → fly 大材小用。如纯临时中间值）

满足三条 = fly 主场。**核心判别词：数据必须是"计算产生的类型化数值对象"。**

---

## 第四部分：Fly 与同品类框架的异同、优势、劣势

### 4.1 相同点（与执行引擎类 Ray/Dask）

1. **Master/Worker 调度模型**：fly、Ray（head+workers）、Dask（scheduler+workers）都是中心调度 + 分布式执行。
2. **数据依赖驱动调度**：fly 的 DependencyGraph（数据就绪→任务就绪）与 Dask 的 task graph 调度、Ray 的 ObjectRef 依赖解析本质相同。
3. **Worker 间直连传数据**：fly 的 Worker-to-Worker DataServer 与 Ray 的对象跨节点迁移、Dask 的 peer-to-peer 传输思路一致。
4. **远程函数抽象**：fly `@as_task` ≈ Ray `@ray.remote` ≈ Dask `delayed` ≈ Celery `@app.task`。
5. **动态任务派生**：fly 支持任务内递归提交任务。

### 4.2 Fly 的优势

| 优势 | 说明 | 对比 |
|------|------|------|
| **C++ 核心性能** | 序列化(bitsery)、压缩(LZ4/ZSTD)、网络(epoll 自研) 全 C++ | Ray/Dask/Celery 纯 Python，热路径有 GIL 与解释器开销 |
| **类型化对象存储一体化** | 数据就绪即触发依赖满足，ObjectCache 双层缓存读，三层降级读取 | Ray 需显式 ObjectRef，Dask 需显式 graph |
| **持久化原生支持** | idx/data 落盘、freeze、load_db 跨进程恢复、failed_tasks.bin 重放 | Ray 对象默认内存态（需配置 spill），Celery 需外部 backend |
| **零拷贝/两段式协议** | FlyBufferPtr 共享所有权、DataResponseProtocol 避免 payload 用户态拷贝 | 多数 Python 框架经 pickle/socket 多次拷贝 |
| **写入事务化** | BEGIN/END/ABORT 段标记 + abort truncate，崩溃脏数据自动丢弃 | 编排器多靠 at-least-once + 幂等 |
| **6 层清晰架构** | BUILD 级 DAG 无循环、宏体系统一、include 合规率 99.1% | 工程纪律强，可维护性好 |

### 4.3 Fly 的劣势

| 劣势 | 说明 |
|------|------|
| **Master 单点** | 所有调度/查询/写注册过单 reactor 线程（但见 5.3 的重新定性） |
| **无 worker 重连/精确恢复** | Master 丢失=全部调度状态丢失 |
| **无背压/流控** | Worker→Master 无 credit 流控；DataResponse 大对象无分片 |
| **协议无版本号** | 无法平滑升级 |
| **生态/社区/UI 弱** | 单一项目，无 Web UI、无插件生态 |
| **部署仅限本地子进程** | `launch_workers` 只能 spawn 本机进程 |
| **调度策略单一** | 仅 FIFO（locality/role 调度未实现） |

---

## 第五部分：可借鉴功能（基于真实定位筛选，含被否决项）

> ⚠️ 本部分经过严格筛选。**早期基于"通用分布式框架"视角的若干建议，经 fly 真实定位（固定流程 EDA 执行引擎，同源于 Ansys SeaScape）核实后被否决**，单列于 5.4，避免误用。

### 5.1 判定基准：fly ≠ 通用分布式框架

fly 的真实定位是**固定流程 EDA 任务执行框架**，其架构与 **Ansys SeaScape** 平台（RedHawk-SC / Totem-SC 的底层分布式平台）基本一致。SeaScape 是这一品类经大规模生产（"tens of thousands of CPU cores"、"near-linear scalability"）验证的成熟实现，是 fly 比 Ray/Dask **更贴切的对标对象**。

**SeaScape 工作范式与 fly 的同源映射**：

| SeaScape 特性 | fly 对应 | 状态 |
|---|---|---|
| fully distributed database，数据 section 成 chunks 分散到分布式磁盘 | db + DataService + DataServer | ✅ 同源 |
| ultra-light central scheduler（head <2GB）+ workers | Master + Worker | ✅ 同构 |
| elastic partitioning，每 worker 分析一个 partition，近线性扩展 | MapReduce 框架 | ✅ fly 已有 |
| map-reduce worker，每 worker 独立分析自己分区 | hybrid worker（一个 task 一个 worker） | ✅ 一致 |
| 跑在共享 POSIX 文件系统 | base_path 共享存储假设 | ✅ 一致 |

### 5.2 真正需要借鉴/补齐的特性（P0–P2）

| 优先级 | 特性 | 印证 / fly 现状 |
|---|---|---|
| **P0** | **弹性 worker（elastic compute）** | SeaScape 核心卖点："starts as soon as a single CPU is ready, more CPUs conscripted as they become available"。fly `launch_workers` 当前固定 spawn，缺运行时 worker 动态加入/退出 |
| **P0** | **数据 locality 调度**（实现空函数） | SeaScape "avoiding network loading and latency effects"。fly `set_locality_preference()` 是空函数，已有 remote_idx 可直接用 |
| **P0** | **调度器保持轻量的纪律** | SeaScape <2GB。指导 fly：调度状态落 db，别堆 Master 内存 |
| **P0** | **stage checkpoint / 断点续跑** | EDA 单任务跑数小时～数天，崩溃不能重来。db 产物天然是 checkpoint，缺"阶段进度"的显式可查询表达 |
| **P0** | **大对象分片传输 + 背压** | EDA 数据几十 GB，单条 DataResponse 不可行；SeaScape 分布式磁盘访问隐含分片/流控语义 |
| **P1** | **partition / 领域数据分区强化** | SeaScape "automatically sectioned into chunks"。可考虑领域数据分区内建支持 |
| **P1** | **Worker role / 资源·license 调度** | EDA 集群异构（工具/license/内存）。`requires` 方向对，需补工具可用性约束；`storage_only` role 需落地 |
| **P1** | **多机 SSH 部署** | SeaScape 云端多机必需。`launch_ssh_workers` 已设计未实现 |
| **P1** | **任务优先级** | 多流程并行/调试抢占时 FIFO 不足。需 priority 进 TaskRequirements |
| **P1** | **流程进度可观测（轻量）** | 需"阶段进度"维度视图，非 Ray 式实时 task 海洋 |
| **P2** | **远程读退避** | 仅防网络抖动（fly 3 次零间隔重试），**不做**通用自动重试 |
| **P2** | **协议版本号** | 平滑升级（MessageHeader 加 `uint8_t version`） |

### 5.3 关于"Master 单点瓶颈"解法方向的重新定性（关键修正）

**早期直觉**（来自通用框架对比）：Master 单点 → 借鉴 Ray 去中心化 / 启用 HandlerThreadPool 把调度分散。

**SeaScape 证伪**：SeaScape 自身就是 **ultra-light central scheduler + 分布式 workers，不走去中心化**。EDA 固定流程决策少，Master 集中调度反而合适。

**正解**（SeaScape 验证的两条路线）：
1. **调度器纪律性保持轻量** —— 状态进 db，Master 内存 <2GB 级别，只做 task↔worker 匹配；
2. **数据 locality 调度** —— 让大多数数据访问不经 Master。

> 注：HandlerThreadPool 把耗时 handler 异步化仍可作为**局部优化**保留，但不是"解决 Master 瓶颈"的正解。

### 5.4 ⚠️ 被否决的通用建议（避免后人误用）

| 通用框架视角的建议 | 被否决的理由 | SeaScape 反证 |
|---|---|---|
| **去中心化调度 / Ray 式每节点本地 scheduler** | EDA 固定流程决策少，集中调度更合适 | SeaScape 是 ultra-light central scheduler |
| **通用每任务自动重试 + 退避**（Celery/Temporal 式） | EDA 失败多确定性（license/参数/OOM），盲目重试浪费 license | SeaScape 不盲目重试；保留人工 `restart_failed_tasks` |
| **Durable Execution / event sourcing 拓扑重放**（Temporal 式） | fly 靠分布式 db 持久化支撑恢复即可，不需拓扑重放 | SeaScape 靠 db 恢复，不做重放 |
| **Saga 补偿模式** | EDA 是"只追加产物"模型，上游只读，下游失败无需撤销上游 | SeaScape 不做 |

### 5.5 明确不需要的特性（SeaScape 也不做）

通用每任务自动重试 / Saga 补偿 / 拓扑 event sourcing 重放 / 去中心化调度 / 复杂动态任务图调度 / Actor 模型（Ray 式有状态 worker）。因为 fly 目标场景流程固定、产物只追加、状态靠 db 持久化。

---

## 第六部分：Fly 的适用领域边界

### 6.1 适用领域（数据=类型化数值对象）

这些领域的数据天然是"应用的内存对象"，能完美利用 fly 的类型化序列化 + 压缩 + 对象缓存：

| 领域 | 核心数据形态 | 与 fly 的契合 |
|---|---|---|
| **数值仿真（EDA / CAE / CFD / FEM / 电磁）** | 稀疏矩阵、解向量、网格、场量 | C++ 原生对象，bitsery 直往返 |
| **大规模线性代数 / 优化求解** | 稠密/稀疏矩阵、张量、梯度 | 类型化、可压缩 |
| **计算化学（MD / DFT）** | 坐标数组、力场、能量、轨迹帧 | 数值数组，类型化、可压缩 |
| **油藏 / 地球物理模拟** | 网格场量、属性体、解向量 | 同 EDA 结构 |
| **科学数值计算后处理**（地震反演/气候模式后处理） | 多维数值数组、网格数据 | 只要处理后数据是数组对象即契合 |
| **ML 数据预处理/特征产物持久化** | 特征张量 | 张量是类型化对象；但 DL 训练本身不适合（无 Actor） |

### 6.2 不适用领域（数据=标准文件格式为主）

| 领域 | 数据形态 | 为什么不适合 fly db |
|---|---|---|
| **多媒体转码分发** | mp4/h264/图片 | 已压缩标准格式，压缩负优化，无法类型化反序列化 |
| **渲染农场（VFX）** | EXR/PNG/视频文件 | 同上，渲染产物是图像文件 |
| **文档/报表批处理** | pdf/office | 同上 |
| **基因组/生物信息流水线** | FASTQ/BAM/VCF | 数据是标准生物格式，db 类型化优势失效（Nextflow 更合适） |

### 6.3 明确不在 fly 射程的领域

| 领域 | 原因 | 归属 |
|---|---|---|
| 深度学习分布式训练 | 需 Actor + 梯度同步/AllReduce，fly 无 Actor 模型 | Ray |
| 高频细粒度任务（百万级 task） | fly 面向重任务，Master 集中调度不适合海量微任务 | Ray |
| 定时 ETL / 数据仓库编排 | 编排器的活，fly 是执行引擎 | Airflow/Dagster |
| 实时/流式计算 | fly 是批处理/流水线 | Flink |
| 通用异步任务队列 | | Celery |
| 超低延迟 MPI 紧耦合计算 | fly 的 TCP/reactor 不替代 MPI | MPI |

### 6.4 一句话定位

> **fly 是"以类型化数值对象为数据媒介的分布式任务流水线框架"——其 db 存储的是应用层定义的、可序列化往返的内存对象（矩阵/张量/场量/向量等），而非标准文件格式。适合所有"核心数据是计算产生的类型化数值对象、需持久化跨阶段流转、且对序列化性能与数据规模有要求"的数值计算领域。**

fly 的真实战场始终围绕**数值/科学计算**——不是因为它必须有求解器（mapreduce/solver 只是跑在 fly 上的应用），而是因为**只有类型化数值对象才能榨干 fly 类型化对象存储 + 高性能序列化的全部价值**。EDA（RedHawk-SC/Totem-SC on SeaScape）是这一类的旗舰，CAE/计算化学/地球物理等同构；标准文件格式为主的数据领域则不在 fly 的射程内。

---

## 第七部分：总结

**Fly 的差异化定位**：一个 C++ 高性能、以类型化分布式对象存储为协作媒介、数据依赖驱动调度的固定流程任务执行框架，同源于 Ansys SeaScape 这一经大规模生产验证的品类。

**架构选型正确**：与 SeaScape 的同源映射证明 fly 的"轻量中心调度 + 分布式 typed db + worker 直传 + 数据驱动调度"方向正确，无需向 Ray 式去中心化或 Temporal 式 durable execution 偏移。

**主要差距**集中在 EDA 定位下仍成立的系统工程维度：弹性 worker、locality 调度、stage checkpoint、大对象分片/背压、多机部署。这些是 SeaScape 已验证、fly 应补齐的能力。

**战略建议**：fly 应坚持数值/科学计算的定位，不追求成为 Airflow（编排器）、Celery（通用队列）或 Ray（通用分布式计算 + AI）。沿 **P0（弹性 worker + locality + 调度器轻量化 + checkpoint + 分片/背压）→ P1（role/license 调度 + SSH 部署 + 优先级 + 可观测）→ P2（协议演进）** 路径补齐，使 fly 在"类型化数值对象的固定流程分布式执行"这一细分赛道上做到既快又稳。

---

## 参考来源

- [Ray GitHub](https://github.com/ray-project/ray) · [Ray Key Concepts](https://docs.ray.io/en/latest/ray-core/key-concepts.html) · [Ray 白皮书](https://sands.kaust.edu.sa/classes/CS345/S19/papers/ray.pdf)
- [Dask GitHub](https://github.com/dask/dask) · [Dask.distributed 文档](https://distributed.dask.org/) · [Scheduling Policies](https://distributed.dask.org/en/stable/scheduling-policies.html)
- [Celery GitHub](https://github.com/celery/celery) · [Celery 配置文档](https://docs.celeryq.dev/en/main/userguide/configuration.html)
- [Prefect GitHub](https://github.com/PrefectHQ/prefect) · [How Prefect Works](https://www.prefect.io/how-it-works)
- [Apache Airflow GitHub](https://github.com/apache/airflow)
- [Temporal GitHub](https://github.com/temporalio/temporal) · [Durable Execution 指南](https://temporal.io/blog/what-is-durable-execution)
- [Luigi GitHub](https://github.com/spotify/luigi) · [Dagster GitHub](https://github.com/dagster-io/dagster)
- [State of Workflow Orchestration Systems 2025 (pracdata)](https://www.pracdata.io/p/state-of-workflow-orchestration-ecosystem-2025)
- [The Rise of the Durable Execution Engine (Kai Wähner)](https://www.kai-waehner.de/blog/2025/06/05/the-rise-of-the-durable-execution-engine-temporal-restate-in-an-event-driven-architecture-apache-kafka/)
- **SeaScape / RedHawk-SC 同源参照**：[SeaScape: EDA Platform for a Distributed Future (SemiWiki)](https://semiwiki.com/eda/ansys-inc/303774-seascape-eda-platform-for-a-distributed-future/) · [Ansys's Big Data Platform (EEJournal)](https://www.eejournal.com/article/20160705-ansysseascape/) · [Ansys RedHawk-SC on Azure (Microsoft Tech Community)](https://techcommunity.microsoft.com/blog/azurecompute/ansys-redhawk-sc%25E2%2584%25A2-on-azure-hold-on-to-your-socks/2262228) · [Ansys RedHawk-SC and AWS](https://aws.amazon.com/blogs/industries/turning-price-performance-upside-down-ansys-redhawk-sc-and-amazon-web-services/) · [Synopsys RedHawk-SC 产品页](https://www.synopsys.com/implementation-and-signoff/signoff/redhawk-sc.html)
- star 数为 2026-06-25 经 GitHub API 实测。

*文档创建日期: 2026-06-25*
