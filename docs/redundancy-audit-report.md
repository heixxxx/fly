# Fly 代码库冗余与过度抽象诊断报告

> 生成日期：2026-07-02（三轮复核）
> 分析范围：`src/` 全部 17 个模块
> 方法：逐文件阅读 + 全仓 grep（`src/`+`qa/`+`big_qa/`+`scripts/`）+ C++ 运行时覆盖率（gcov/lcov，74.1% 行覆盖）+ **设计文档交叉验证**

---

## 复核原则

**以文档为准**。对"文档有描述但代码无调用"的项，区分四种命运：
- **真无用**：文档无规划 + grep 0 调用 + 覆盖率 FNDA:0 → 可删
- **被替代/降级**：文档曾规划但已降级（roadmap 决策记录）或被替代方案架空 → 可删（连同文档）
- **未完成功能**：文档规划且仍排期 → 保留（属 TODO，非死代码）
- **有意设计**：文档给出明确意图且合理 → 保留

### 单测专用接口的特别复核

部分接口仅被单元测试调用（生产代码 0 消费）。这类接口有三种正当性，需逐个甄别：
1. **可测性钩子**：为让生产功能可单测而预留的注入点（如 Transport 纯虚供 mock 注入）——保留
2. **算法验证载体**：测试间接验证了生产共享逻辑（如 low tier 测试验证 LFU 算法，该算法 high tier 也用）——测试有保留价值
3. **自洽的孤立闭环**：被测代码 + 测试形成闭环，与生产无任何关联——可删

本轮对第二类的 `compress_chunk`/`compression_utils`、`StorageManager::get_writer`、`ReadCache low tier` 三项做了单测意图复核，结论见 §2.1/§2.2/§2.3 各自的"单测意图复核"小节。

---

## 第一类：真无用代码（文档无规划 + 0 调用，建议删除）

### 1.1 Solver — GMRES 向量运算脚手架（文档无 GMRES 规划）

`solver/module.md` 只描述 RAS（图扩展 + 子域求解 + 粗网格校正 + omega 策略），**完全未规划 GMRES/Krylov**。`matrix-solver-analysis.md:361` 仅引用一篇 2001 年 PCG 论文作为理论参考，非实现路线。

| 位置 | 内容 | 判定证据 |
|------|------|---------|
| `solver/export/solver_export.cpp:194-240` | 7 个 `ex_slv_vec_*`（norm/dot/scale/axpy/sub/back_solve/xpay，~47 行） | `vec_back_solve` 是 Hessenberg 上三角回代（GMRES 专属）；grep 全仓 0 调用；`py/__init__.py` 未 re-export |
| `solver/export/solver_export.cpp:166-190` | `EXSlvSparseMatrix` 类（from_coo/matvec/size） | grep 全仓 0 调用 |

**判定：真无用。** 文档无 GMRES 路线，是为不存在的求解器预制的脚手架。

### 1.2 Solver — ORAS 变体（文档列为理论参考，非实现目标）

`matrix-solver-analysis.md:23` 把 ORAS（优化 Schwarz 法）列在"理论方法对比表"和竞品（FreeFem++）能力清单里，但 `solver/module.md` 明确 fly 只做 RAS。

| 位置 | 内容 | 判定证据 |
|------|------|---------|
| `solver/export/solver_export.cpp:95-125` + `solver.h:37-41` + `solver.cpp:115-144` | `ex_slv_extract_subdomain_matrix_oras` 导出 + `extract_subdomain_matrix_oras` 实现（~55 行） | grep 全仓 0 调用；非 ORAS 版 `extract_subdomain_matrix` 被 ras.py/ras_graph.py/3 个 qa scripts 大量使用 |

**判定：真无用。** 注意区分：非 ORAS 版是热路径（活），仅 ORAS 版死。

### 1.3 Solver — Python 死导入

| 位置 | 内容 | 判定证据 |
|------|------|---------|
| `solver/py/ras_graph.py:731-734` | import `ex_slv_graph_expand_overlap`/`extract_subdomain_matrix`/`find_outside_connections` 但函数体未用 | ras_graph.py 用纯 Python BFS + numpy；但这 3 个 C++ 函数**被 `qa/scripts/debug_ras_configs.py` 真实调用**（L78-107）→ C++ 函数活，仅 ras_graph.py 内的 import 死 |

**判定：ras_graph.py 的 import 真无用（删 import 不删函数）。** 首轮报告误把 C++ 函数也说成死，已纠正——qa/scripts 是活的生产调试工具。

### 1.4 Network — 运行时 0 调用的接口方法（FNDA:0）

| 位置 | 内容 | 判定证据 |
|------|------|---------|
| `connection_manager.h:40` + `tcp_connection_manager.cpp:143` | `ConnectionManager::recv(conn_id, buffer, max_size)` | **FNDA:0**；Reactor 走 `poll()`→`Transport::recv(fd,...)`。注意：`Transport::recv` 是热路径（7 处调用），死的只是 `ConnectionManager::recv` 这一个特定签名 |
| `message_protocol.h:99` | `MessageProtocol::decode_header()` 静态方法 | grep 全仓 0 调用 |
| `reactor.h:72` + `reactor.cpp:107` | `Reactor::set_handler_pool()` | **FNDA:0** |

**判定：真无用。** 接口完整性残留，无文档规划。

### 1.5 Common — 零使用 typedef

`common/cpp/common_types.h` 中 11 个别名 grep 全仓（src+qa+big_qa+scripts）0 调用，仅 `common_types_test.cpp` 引用：

`CMWeakPtr` / `CMStaticPointerCast` / `CMDynamicPointerCast` / `CMConstPointerCast` / `CMReinterpretPointerCast` / `CMSet` / `CMList` / `CMDeque` / `CMQueue` / `CMStack` / `CMMapKV`

**判定：真无用。** "为切 absl 预留"在文档中无规划，且生产代码 0 使用。

### 1.6 Serialization — 死宏（文档自相矛盾）

`serialization/module.md:57` 明确 `FLY_FIELD` 是"统一宏（推荐）"，L137-147 说明 FLY_FIELD 是唯一展开路径。文档 L60-64 虽列出 `FLY_STR/FLY_VEC/FLY_MAP/FLY_OBJ/FLY_BOOL`，但它们是 `FLY_FIELD` 自动分发前的**旧 API**，无业务结构体直接调用。

| 位置 | 内容 | 判定证据 |
|------|------|---------|
| `serialization_macros.h` | 8 个旧字段宏 `FLY_STR/FLY_STR_U16/FLY_STR_U32/FLY_VEC/FLY_VEC_F/FLY_MAP/FLY_OBJ/FLY_BOOL` | grep 全仓 0 调用（被 FLY_FIELD 取代） |
| `serialization_macros.h:49-56` | `FlyTrustedConfig` 重复字段（`Endianness_`+`Endianness` 等 3 对） | 纯冗余赋值 |
| `serialization_macros.h:341,309` | `FLY_ENCODE_TO_STREAM` / `FLY_DECODE_FROM_BUFFER` | grep 0 调用 |

**注意 — cereal 后端切换（`serialization_macros.h:7-22,349-356`）不删**：`module.md:156` 明确"宏抽象层...未来可替换为 cereal/protobuf"是有意设计意图。虽 0 实现，但属文档承认的预留扩展点，非死代码。

### 1.7 Export — 死宏

`export_macros.h:41,43`：`FLY_EXPORT_STATIC_METHOD`、`FLY_EXPORT_PROPERTY` — grep 全仓 0 调用。

---

## 第二类：被替代 / 降级的代码（文档曾规划但已弃，建议连文档一起清理）

### 2.1 Python ReadCache low tier（被 C++ ObjectCache.low 取代）

`zero-copy-analysis.md:70-83` 确立 C++ 侧 `FlyBufferPtr` shared_ptr 零拷贝取代 Python bytes 路径。`database.py:90-99` 实际只在 `cache=="high"` 时调用 Python ReadCache，且只 put/get `"high"`。

| 位置 | 内容 | 测试上下文 |
|------|------|-----------|
| `storage/py/read_cache.py` 的 `self._low` 整套（~40 行） | 生产 0 写入；`"low"/"none"` 走 C++ ObjectCache low tier | `test_read_cache.py` 11 个测试中 **9 个测 low tier**（put/get/miss/lru_eviction/protection_period/hard_limit/remove/clear/overwrite/scoring） |

**单测意图复核**：low tier 测试**真实且有质量**——它们测的是 LFU 算法（淘汰、保护期、硬上限、read_count 评分）。这些算法**与 high tier 共享同一套淘汰逻辑**（`_evict` 方法 low/high 共用），所以 low tier 测试**间接验证了 high tier 的淘汰正确性**。但 high tier 存 Python 对象，难造大对象触发淘汰，所以 low tier 成了 LFU 算法的事实测试载体。

**这造成一个权衡**：删 low tier 代码会丢失 LFU 算法的测试覆盖（high tier 无独立淘汰测试）。处置选项：
- (a) 删 low tier 代码 + 保留 low tier 测试改为直接测 `_evict` 私有方法（算法仍在，载体独立）
- (b) 保留 low tier 作为 high tier LFU 的"可测替身"（当前态，代码冗余但测试有价值）
- (c) 删 low tier 代码 + 删 low tier 测试，给 high tier 补独立淘汰测试（最干净但工作量大）

**判定：被替代 + 测试有算法验证价值。** 不是纯死代码。推荐 (a)：代码可去，但 `_evict` 的算法测试应保留（重构为直接测算法而非通过 low tier）。

**判定：被替代。** Python ReadCache 实质只剩 high tier（存 Python 对象引用，C++ std::any 无法持有）。

### 2.2 compress_chunk / compression_utils 子系统（设计目标已降级 + 被替代）

- `compressor.h:40` 注释 "independently decompressible block for streaming"（为流式分片预留）
- 但 `roadmap.md` 决策记录 ⑤：**F4 大对象分片+背压降级**（"出现实测证据时再做"）
- 且流式压缩已由 `CompressingStreamBuf/DecompressingStreamBuf` 承担，走 `compress(string_view)`/`decompress_to`，不经 chunk 路径

涉及代码构成一个完整的"分块压缩 + 流式 I/O 序列化"子系统：

| 位置 | 内容 | 测试上下文 |
|------|------|-----------|
| `compressor.h:41-42` + LZ4/ZLIB/ZSTD 实现 | `compress_chunk`/`decompress_chunk` | `compressor_test.cpp` 的 `StreamingChunkRoundTrip` 测试（分块压缩往返） |
| `compression_utils.h/.cpp`（84 行） | 4 个函数：`serialize_chunk`/`deserialize_chunk`/`write_compressed_to_stream`/`read_compressed_from_stream` | `compressor_test.cpp` 的 `SerializeDeserializeChunk`/`RoundTripThroughFile`/`DeserializeMultipleChunks` 等（含文件流往返、截断恢复、多 chunk 串联） |

**单测意图复核**：测试**真实且有意义**——验证了"分块压缩 → 序列化 → 文件写入 → 读回 → 反序列化 → 解压"的完整往返，含截断/多 chunk 等边界。这不是凑覆盖率的空测，而是**对一个完整功能子系统的验证**。问题在于：该子系统**生产零消费**（生产用 streambuf 走另一条路），且其设计目标（F4 流式分片）已在 roadmap 降级。

**判定：被替代 + 降级。** 测试有质量但保护的是一个生产不用的子系统。删除需连同 `compressor_test.cpp` 的 `StreamingChunk*`/`CompressionUtils*` 共约 9 个 TEST 一起。首轮报告把它误判为"纯死代码/纯别名冗余"是错的——它是"高质量测试守护着已降级的功能"。

### 2.3 StorageManager::get_writer（生产不可达，测试自洽）

`storage_export.cpp` 只导出 `get_or_create_database`/`close_all`/`reset`，**不导出 get_writer**。

| 位置 | 内容 | 测试上下文 |
|------|------|-----------|
| `storage_manager.h:23,30` + `.cpp` | `get_writer(worker_id)` + `writers_` map，FNDA:9 | `storage_manager_test.cpp` 的 `GetWriterByWorkerId`/`GetWriterCreatesWorkerDirectory`/`GetWriterReturnsSameInstanceForSameWorkerId` 等 4 个 TEST |

**单测意图复核**：测试**只验证 get_writer 自身行为**（按 worker_id 缓存去重、创建 `/tmp/fly_worker_<id>` 目录、close_all 清理），**不间接保护任何生产功能**——生产代码（worker_agent/master_agent）用的是 `Database::get_writer_id()`（完全不同的方法，8 处调用，导出为 `get_writer_id`）。这是"自洽的孤立测试"：被测代码 + 测试形成闭环，与生产无关联。

**判定：可删（连测试一起）。** `get_writer` + `writers_` 字段 + 4 个 TEST 全部是孤立闭环。

**注意**：`StorageManager` 类本身和 `get_or_create_database`（FNDA:13）是活 API，**不可删整个类**。

### 2.4 死配置项（文档推荐但 C++ 不消费，或已废弃）

经全仓核实消费方：

| 配置项 | 消费方数 | 文档状态 | 判定 |
|--------|---------|---------|------|
| `backup_decay_interval` | 0 | `architecture.md:477` 推荐 | 文档承诺未实现 |
| `backup_decay_factor` | 0 | `architecture.md:478` 推荐 | 同上 |
| `large_file_threshold_kb` | 0 | `core/module.md:66` 推荐（标记废弃 `large_file_threshold`） | 文档承诺未实现 |
| `block_size` | 0 | `architecture.md:134` 推荐 | 文档承诺未实现 |
| `track_writes` | 0 | `architecture.md:135,425` + `python-api/module.md:549` 推荐 | **功能（write_provenance_）已默认全开实现**，开关本身死 |

**对照（活配置项确认）**：`auto_backup_enabled`(2)、`backup_threshold`(1)、`backup_replicas`(2)、`aggregation_threshold`(1)、`compression_threshold`(2)、`serialize_chunk_size`(2)、`data_server_threads`(2)、`locality_scheduling_enabled`(3) 均有真实消费方。

**判定：文档承诺未实现 + 开关死。** 处理方式：要么补实现，要么从 config 默认表 + 推荐文档同时移除。`track_writes` 特殊——功能已实现且默认开，应移除开关（或文档改为"已默认启用，开关无效"）。

---

## 第三类：有意设计（文档给出明确意图且合理，保留）

> 这部分是首轮报告判定为"过度抽象/重复"但经文档验证后**推翻**的项目。

### 3.1 task 状态机三处分散 = 三个正交关注点（有意设计）

`docs/task/module.md` + 代码证据：
- **DependencyGraph**：管"数据依赖就绪 vs 未就绪"（二值）——高频反向索引查询，无 worker 概念
- **TaskManager**：管"PENDING/RUNNING/COMPLETED/FAILED/CANCELLED"（五值）——带锁元数据 CRUD
- **MasterAgent**：不存状态，只编排决策（graph + worker_manager 串联）

三处管理的是**不同状态机**。合并会破坏 `restart_failed_tasks`（graph + TaskManager 可独立重建）和调度热路径（graph 无锁读 vs metadata 有锁）。

### 3.2 TaskManager 双索引 = 热路径优化（有意设计）

`module.md:62-95` 给出量化复杂度：`has_tasks_with_status` O(1)、`get_tasks_by_status` O(k)。`schedule_tasks()`（每次 write_register/task_complete/heartbeat 都触发）高频调用 `has_tasks_with_status(RUNNING)`。针对 ≤100 task 量级单 map 也可，但双索引有文档化依据，非过早优化。

### 3.3 HeartbeatMonitor 独立 = 调度/策略分离（有意设计，文档简略）

MasterAgent 持"何时检查"（线程+cv），HeartbeatMonitor 封"怎么判定"（超时算法+状态写回）。便于单测。文档论证不足，建议补 module.md。

### 3.4 TaskExecutor = GIL 安全回调容器 + C++/Python 边界（有意设计）

非空壳——`set_exec_func`/`clear_exec_func` 内部做 GIL 管理（`PyGILState_Ensure/Release`），因 `std::function` 持 Python 对象引用。让 agent 核心不 `#include <Python.h>`。与 `WorkerAgentContext` 的 std::function 回调解耦是同一设计模式（`agent/module.md:295-301`）。

### 3.5 task_modules_/args_/vars_ 三 map vs TaskManager = 调度元数据 vs 执行负载分离（有意设计）

`TaskMetadata` 刻意不存 module/args（只有 name/inputs/outputs/config/capabilities）。三 map 存"执行负载"（Python 模块名、序列化参数），只有 `assign_task_to_worker` 和 `build_failed_record` 读。合并进 TaskMetadata 会让 scheduler 接触执行负载 + 查询时无谓拷贝大 args。可改进点：三 map 合一为 `TaskPayload`（减少锁竞争），但非过度抽象。

### 3.6 ObjectCache 两层 LRU（有意设计）

`module.md:206-224` 收益矩阵：low 省 IO（对 bytes 有效），high 省反序列化（对复杂对象，`read-write-optimization.md` 实测 list 读取 34.1ms 中 pickle.loads 是主瓶颈，bytes 仅 4ms）。high 层生产路径真实填充（`database.h:300` `read_object<T>` 命中后 put_high）。

### 3.7 C++ ObjectCache vs Python ReadCache 算法重复 = 语言隔离硬约束（有意设计）

`object_cache.h:2` 注释 "Mirrors the Python ReadCache semantics"。隔离原因：C++ high 层存 `std::any` 持 `CMSharedPtr<T>`，Python high 层存 Python 对象引用——**无法统一存储**。可改善点：LFU 参数（30s/1.5×）抽共享常量，目前手动镜像。

### 3.8 Database（门面）vs DataService（单例元数据）= 内容 vs 位置分离（有意设计）

`module.md:13-22` 职责表清晰：Database 管"数据内容读写"，DataService 管"全局索引+远程协调"。**唯一瑕疵**：TempStore 双持（`Database::temp_store_` + `DataService::temp_eviction_store_`）是真实冗余，文档未解释为何两层都有。

### 3.9 cereal 后端切换框架（有意设计，预留）

`serialization/module.md:156` 明确意图。虽 0 实现，属文档承认的预留扩展点。

### 3.10 Transport 纯虚接口 = 测试注入价值（混合：测试价值真，扩展价值未兑现）

`module.md:262` 声称"支持未来 UDP/RDMA"，但测试 `tcp_connection_manager_test.cpp:110-111` 自证 udp/rdma throw。**真正立得住的是测试 mock 注入**（DataClientPool 暴露 Transport 注入构造）。建议：保留接口（测试价值），文档停止以 UDP/RDMA 为由。

### 3.11 DataClient vs DataClientPool 并存 = 并发限流差异化（混合：设计有意，实现重复）

`module.md:152-178` 明确区分：DataClient = 无并发控制单次原语；DataClientPool = 信号量限流 + ReadError 契约（为 TIER2 重试演进）。**实现层重复**（两段式协议逐行复制，`data_client.cpp:61-117` vs `data_client_pool.cpp:96-192`）——应组合而非复制。

### 3.12 HandlerThreadPool = 文档自承未实现的预留（当前死，终态有意）

`reactor-async-design.md:261,654` 明确"复用现有 HandlerThreadPool（已定义，当前未使用）"，规划阶段 2 启用。`ARCHITECTURE_REVIEW.md §3.1` 标 [待修复]。**当前 set_handler_pool FNDA:0 是死的**，但文档明确排期。处置：要么按 spec 落地，要么标记为"未实现设计"。

---

## 第四类：重复代码（应抽取，与设计意图无关）

| # | 位置 | 问题 |
|---|------|------|
| 1 | BE32 解析 7+ 处 | `message_protocol.h:38-42,69-73,87-91`（同头 3 次）+ `data_client.cpp:69` + `data_client_pool.cpp:113` + `metadata_client.cpp:65` + `data_server.cpp:219` + `worker_agent.cpp:346`。`get_total_size()` 已封装，应抽 `read_be32` |
| 2 | DataClient/DataClientPool 协议重复 | 见 3.11，设计有意但实现重复，应组合 |
| 3 | master/worker 数据读取函数逐字重复 | `request_remote_data`/`request_data_from_worker`/`set_direct_compressed_read_handler` |
| 4 | worker_agent 5 套 Pending+cv | 可模板化 `PendingRpcMap<Key,T>` |
| 5 | ras_graph.py coarse grid 复制粘贴 | `347-405` vs `480-522` ~40 行 |
| 6 | C++ solver.cpp BFS vs Python ras_graph.py BFS | 同算法两份实现（C++ 版 qa/scripts 在用，Python 版生产在用） |

---

## 第五类：架构边界问题

| # | 问题 | 证据 | 判定 |
|---|------|------|------|
| 1 | common 反向依赖 serialization | `worker_context.h:5` include fly_buffer.h | 真问题（循环依赖风险） |
| 2 | FlyBuffer 归属错位 | 跨 storage/network/common 用，却在 serialization | 真问题 |
| 3 | worker_context.h 错放 common | 仅 agent+storage 用（`WorkerAgentContext`，非 `WorkerContext`）；放 common 是 module.md 明确记录的有意决策（打破 storage↔agent 循环依赖），非随手乱放 | 有意妥协 + 文档过时（详见 `docs/fifth-class-verification.md`） |
| 4 | Database/DataService TempStore 双持 | 见 3.8 | 真冗余 |
| 5 | log 模块 Boost.PP 依赖 | `logger.h:74-115` 格式化宏 | 轻度越界 |
| 6 | common 充当杂物间 | writer_id/write_context_hash/test_helpers 仅 1-2 模块用 | 轻度 |

> 注：task 状态机三处分散（首轮列为边界问题）经文档验证为有意设计（3.1），**移出边界问题清单**。

---

## 第六类：未完成功能（文档承认的 TODO，非死代码，保留）

| 位置 | 内容 | 文档状态 |
|------|------|---------|
| `ras_graph.py:794-803` | `aitken` omega 策略分支 | `solver/module.md:63` "adaptive = Aitken-like"；实现不完整（check 阶段只写 adaptive） |
| `mapreduce.py:269+276` | Partition 双重执行 | 疑似 bug 或设计缺陷，需单独确认 |
| Freeze 后处理（idx 合并/merged.idx/_META） | master 无 IdxRequest handler | `roadmap.md` 决策②**降级**（load_db worker 齐备时非阻塞） |
| `arch.py:701-704` Locality "尚未实现" | — | `roadmap.md` 已确认**已实现**（文档过期） |

---

## 量化汇总（最终版）

| 类别 | 项数 | 估算行数 | 风险 | 处置 |
|------|------|---------|------|------|
| 一、真无用 | 7 组 | ~280 行 | 零 | 可删（GMRES/ORAS/vec、CM 别名、recv/decode_header/set_handler_pool、死宏、死导入） |
| 二、被替代/降级 | 4 组 | ~200 行 + 测试 + 文档 | 低 | 删代码 + 删测试 + 改文档 |
| 三、有意设计（保留） | 12 项 | — | — | 不动，部分补文档 |
| 四、重复代码 | 6 项 | ~150 行（净减） | 低-中 | 抽取公共逻辑 |
| 五、边界问题 | 6 项 | 文件迁移 | 中-高 | 重构 |
| 六、未完成功能 | 4 项 | — | — | 按 roadmap 排期 |

---

## 与首轮报告的关键差异

| 首轮判定 | 二次（覆盖率） | 三次（文档）最终 |
|---------|--------------|----------------|
| compression_utils 整文件死 | 94.9% 覆盖，误判 | 被替代+降级（测试活跃，生产零消费，roadmap 降级 F4） |
| compress_chunk 纯别名冗余 | FNDA 1~3，非别名 | 被替代+降级（有独立实现，但设计目标降级） |
| task 状态机三处分散=重复 | — | **有意设计**（三个正交关注点） |
| TaskManager 双索引=过早优化 | — | **有意设计**（热路径 O(1)） |
| HeartbeatMonitor=薄包装 | 100% 覆盖 | **有意设计**（调度/策略分离） |
| TaskExecutor=空壳 | 74.3% 覆盖 | **有意设计**（GIL 边界） |
| C++/Python 缓存重复 | — | **有意设计**（语言隔离硬约束） |
| graph_expand_overlap/find_outside_connections 死 | — | **活**（qa/scripts 在用） |
| StorageManager 类可删 | — | **误判**（get_or_create_database FNDA:13 活） |
| ConnectionManager::recv 死 | FNDA:0 确认 | 真死（仅此签名，Transport::recv 活） |
| HandlerThreadPool 死 | set_handler_pool FNDA:0 | 当前死，文档排期（终态有意） |

---

*本报告基于 grep + 覆盖率 + 设计文档三轮交叉验证，未修改任何代码。*
