# Storage 模块 — 存储层

> **最后核对：2026-09-02 对照 b48b626+ 源码。**
> 2026-08-29 ~ 09-01 storage 面经历四波大改（恒流式读写 / temp 恒落盘 / 缓存双池与单层化 / 流插件化管线 + sendfile 分片 serve），本文全部按当前源码重写；分片与流式传输的**权威设计记录**见 [`docs/chunked-transfer-design.md`](../chunked-transfer-design.md)（§14 为现行语义）。

## 模块概述

**位置**: `src/storage/`（`cpp/` C++ 核心、`py/` Python 编排、`export/` nanobind 导出）

存储层是 Fly 框架的核心数据管理模块，负责数据的写入、聚合、索引管理、读取（本地 + 远程）、压缩和数据库生命周期管理，设计为 Master 和 Worker 共用的统一层。

**当前架构基调（均为 2026-08-30/31 用户裁定，不可回退）**：

- **恒流式**：写侧统一 `open_write_stream → finish_and_commit`（`streaming_write_threshold` 开关已删）；读侧常规读统一流式消费（`streaming_read_threshold` 开关已删，代码零残留）。仅非反序列化场景（backup 副本拉取、C++ `read_object<T>`）保留全量拉取路径。
- **temp 恒落盘**：temp 压缩 record 不驻内存（去「①形态」裁定），write-through 落 `.temp.data_*.dat`；temp LRU/eviction 整体退役（`temp_store_size` 配置键已删）。
- **读缓存双池**：Python `ReadCache` 主池（low/high 等级标记）+ temp 池；C++ `ObjectCache` 单层化（仅 high，反序列化对象）。**两侧均不再缓存压缩字节**（low-tier 压缩缓存已取消，读恒走数据源）。
- **流插件化管线**：写/读方向的压缩/CRC/块格式化变换以「块」为粒度组合成 Stage 插件（`pipeline.h`），磁盘格式、DataServer 分片、PeerRpc 流式共用同一对 sink/source 抽象。

---

## 核心组件

| 组件 | 文件 | 职责 |
|------|------|------|
| Database | `cpp/database.h/cpp` | 统一存储接口：流式写编排、读取入口、事务段、temp/backup/var |
| Database (Python) | `py/database.py` | Python 编排层：恒流式读写入口、缓存 populate、DB chain（`_DB_META`） |
| DataService | `cpp/data_service.h/cpp` | 单例内存索引（local/remote/worker registry）+ 三级 fallback 读编排 + WBQ 持有 |
| DataServer | `cpp/data_server.h/cpp` | epoll + send 线程池，响应远程数据请求（整帧快路径 + L2 分片路径） |
| WritePipeline / ReadPipeline | `cpp/pipeline.h/cpp` | 流式块管线：Compress/Crc/BlockHeader/CrcVerify/Decompress Stage 插件 |
| CompressingStreamBuf | `cpp/compressing_streambuf.h/cpp` | 写方向 ostream/sink 薄壳适配（切块+管线装配） |
| DecompressingStreamBuf | `cpp/decompressing_streambuf.h/cpp` | 读方向 istream/source 薄壳适配（块拉取+管线装配） |
| ChunkSource 家族 | `common/cpp/chunk_source.h` + `cpp/{disk,memory,network}_chunk_source.*` | 拉取式输入源：DiskChunkSource（pread）/ MemoryChunkSource / NetworkChunkSource（接收线程+有界队列） |
| ObjectCache | `cpp/object_cache.h` | C++ 单层 LRU 读缓存（反序列化 typed 对象，high 层） |
| ReadCache | `py/read_cache.py` | Python 双池解压对象缓存（主池 low/high 等级 + temp 池） |
| DataWriter | `cpp/data_writer.h/cpp` | 纯落盘写入聚合器（正式 + temp 双 writer，增量写/滚文件/段事务） |
| DataReader | `cpp/data_reader.h/cpp` | 纯读取字节流（entry 定位 + 区间读） |
| WriteBackQueue | `cpp/write_back_queue.h/cpp` | 异步落盘队列（单消费线程 FIFO，背压 + clear_pending） |
| StorageManager | `cpp/storage_manager.h/cpp` | Database 生命周期管理（进程级复用同一 C++ 实例） |
| 导出层 | `export/storage_export.cpp` | `_fly_storage` 模块：`ex_stg_open_read_stream` / FlyStream / Database 绑定 |

---

## 磁盘格式与块管线

### record 格式

磁盘上的一个对象 = 一条 **record = 块流 + trailer**（纯追加，无占位/回填）：

```
块记录 = [i32 unc][i32 comp][u64 crc][payload]   （LE 内存序，块头 16B）
  - crc 覆盖 payload（写入时刻锚点，覆盖 磁盘→server→网络→client→解压 全生命周期）
  - comp == unc 隐式标记 raw 直通块（块级压缩率不达标，省对端解压）
trailer = ObjectHeader::serialize_trailer
  - py_name、total_size、chunk_count、block_comp_lens（B' 块表）、实际生效压缩类型
```

**实际生效压缩类型**：全 raw 单块流（旧「小对象跳过压缩」语义）时 trailer 记 NONE，读侧按实际格式选解压路径。

### Stage 管线（`pipeline.h`）

```
写方向:  明文流 ──切块──▶ [CompressStage] ─▶ [CrcStage] ─▶ [BlockHeaderStage] ─▶ emit(sink/流)
读方向:  source ◀──拉块── [CrcVerifyStage] ─▶ [DecompressStage] ─▶ 明文块
```

- **CompressStage**：块级 **85% 规则**——压缩输出 ≥ 85% × 明文则放弃压缩，encoded 切回 plain 视图（零拷贝，`comp == unc`）；流级 `raw_threshold`（即 `compression_threshold`）——尾块 ≤ 阈值时整块 raw 直通（复刻旧「首块小对象跳过压缩」）。
- **CrcStage / CrcVerifyStage**：对交付字节（压缩态或 raw）算/验 CRC。失配 = `failed`，零容忍。
- **DecompressStage**：`comp == unc` raw 直通（视图零拷贝）；解压输出长度失配也置 `failed`（CRC 已过但解压败 = 实现层缺陷，零容忍）。
- **WritePipeline**：按 `serialize_chunk_size`（默认 4MB）切块逐块走 Stage；产出 `total_uncompressed` / `chunk_count` / 块表。
- **ReadPipeline**：拉取式（PullFn）。**wire 块头 16B 上界校验**（`kMaxWireBlockBytes = 64MB`）：磁盘位翻转/坏流把 size 解成 garbage 时直接 `failed`，不进 resize（防未捕获 bad_alloc）；**干净 EOF vs 中途截断区分**——恰好耗尽 = EOF，块头/块数据不完整 = `failed`（消费方必须按损坏处理，不得当 EOF）。
- Stage 为有状态插件（scratch 跨块复用，零每块分配），可独立增删；PeerRpc 流式复用同一对抽象（第 3 步装配 PeerFrameStage + START/END 会话），见 [`docs/rpc-stream-pipeline.md`](../rpc-stream-pipeline.md)。

CompressingStreamBuf / DecompressingStreamBuf 是流接口薄壳：变换逻辑全在管线，本类只做 ostream/sink 与 istream/source 适配和统计转发。

---

## Database

### 写路径（恒流式）

**Python 侧唯一路径**（`write_object` / `_write_temp`，`py/database.py`）：

```
open_write_stream(name, py_name, temp=false)
  │
  ├─ 1. 注册预许可（preliminary register，不带 size，master 不激活可见性）
  │     frozen/provenance/DUPLICATE 拒绝即失败：零序列化零落盘零段事务副作用
  ├─ 2. 段事务 mark_begin（transaction_mode 激活时，记 data 文件偏移回滚点）
  ├─ 3. begin_incremental（可能先滚文件——增量写不跨文件）
  ├─ 4. pickle.dump(stream)：明文 → CompressingStreamBuf(sink) → 压缩块
  │     逐块构造 WriteRequest 入 WBQ 后台落盘（序列化生产与盘写流水重叠；
  │     WBQ 单消费线程 FIFO 保证块顺序；high_watermark 背压节流生产端）
  └─ 5. finish_and_commit(backup, populate_cache)
        ├─ WBQ finish 单元：finish_incremental（trailer 已入队，FIFO 保证在块后）
        │   + flush_checked；promise 通知任务线程盘写完成
        ├─ 正式 register_write（带真实压缩 size）——此刻 master 才标记就绪、
        │   激活可见性；即「注册时数据已完整在盘上」
        └─ on_write_completed（entry 登记 local_idx，COMPLETE）→ on_object_flushed
```

未 commit 析构 = 放弃：残块无 trailer 结构上不可读（commit marker 语义）+ 段事务 ABORT 兜底。frozen db open 返回 None，调用方 raise。

**C++ `write_object<T>`**（导出对象 `obj._write_to_db` 走此路径）：ostream 模式 CompressingStreamBuf 构造完整 record（header + 块流 + trailer）→ `commit_write`：`on_write_started` → `register_write`（同步 RPC，此时激活可见性）→ 同步 `record_write`（task 写追踪）→ WBQ 执行 `write_record_checked + flush_checked`（异步落盘）。注册先于盘写完成——本地读由 per-db cv 等 COMPLETE、远程读由 NOT_READY 轮询兜底。`populate_cache` 参数保留 API 兼容（no-op，§4.7 low-tier 取消）。

`write_pickle_bytes` / `compress_pickle_bytes` / `write_temp_pickle` 已删除（T2b/T2c/T2d 裁定：调用仅存在于测试的过期 API）。

### 写入事务（task 失败时的脏数据清理）

worker task 的写入被 BEGIN/END 段标记包裹。task 失败时整段撤销：

- **BEGIN**：task 首次写入时打，记录 data 文件偏移作为回滚点。**正式 + temp 双 writer 同步开段**（`mark_write_begin` 双打——temp 写已纳入 task 追踪，混合写时两族共享同一事务边界；空段 abort 是 no-op）。
- **END**：task 成功时打，提交段内所有 ADD 进 idx `entries_`。
- **ABORT**：task 失败时 `abort_task_writes` 执行：`clear_write_back`（清 WBQ 未落盘脏写）→ `drain_write_back`（等在跑的那个自然完成）→ 双 writer `abort_segment`（idx ABORT + data truncate 回滚点）→ 清 DataService local_idx / ObjectCache 内存。

master 直接 write_object 不打标记（隐式事务，ADD 立即生效）。崩溃（无 END/ABORT）→ load_db 时 pending 区自动丢弃未提交的脏 ADD。详见 `docs/issues/001-failed-task-rerun-write-duplication.md`。

### Temp 写入（temp 恒落盘）

temp 数据 **write-through 落盘**（2026-08-30 去「①形态」裁定），不再有「仅内存」形态：

- Database 构造时与正式 writer 同生命周期创建 **temp_writer_**（同 writer_id，文件 `.temp.data_{wid}_{NNN}.dat` + `.temp.{wid}.idx`，恒落 db_path 目录——temp 必须自包含以支撑 task 级断点跨进程恢复）。惰性创建会漏段，故禁止。
- 两条写路径，语义一致（`on_temp_write_started` INCOMPLETE+is_temp → 盘写 flush 完成 → `on_temp_write(disk_entry)` COMPLETE → `record_write` 纳入 task 追踪 → `register_write` master 可见）：
  - `put_temp_data(FlyBufferPtr)`（C++）：write-through——`write_record_checked + flush_checked` 同步落盘，idx ADD 落盘必须 NOW（否则 task END 标记先于 ADD 落盘，恢复时段内无记录）。
  - `open_write_stream(temp=true)` → `open_temp_write_stream`（T2d 流式化）：pickle.dump 直入 temp_writer_ 增量直写（内存 R+常数，取代旧 write_temp_pickle 的 R+2C 整对象缓冲）；块经 WBQ 后台落盘，FIFO 保证 trailer 在所有块后。
- 落盘失败即写失败，**不做「仅内存」降级**（COMPLETE 的 temp 输出必在盘上，否则恢复方静默重算/悬空）。
- **跨进程恢复**（task 级断点）：worker 启动加载 `{wid}.temp.idx` → `restore_temp_entries` 灌 local_idx（is_temp=true + entries，无内存数据）→ 已完成 task 的 temp 输出对下游 ready。
- **生命周期**：freeze = 阶段完成，中间态作废——`cleanup_temp_entries`（内存）+ `cleanup_temp_files`（删 `.temp.*` 文件，幂等，master/worker 侧均调）；frozen db 构造时兜底删残留（覆盖 freeze 删除中断/广播丢失窗口）。
- temp 对象 remove：temp idx 命中即完成（内存 idx 清理 + 磁盘 idx REMOVE 条目；data 为共享滚动文件不删，空间由 freeze 批量回收）。

`temp_store_size` 配置键、`temp_compressed_data_` 内存 LRU、`temp_eviction_store_` 磁盘溢出层均已删除。

### 读路径

**Python 常规读恒流式**（`read_object` → `ex_stg_open_read_stream`）：

```
read_streaming(name)
  │
  ├─ TIER1 (本地): find_chunked_location（COMPLETE + entries 定位 .dat 区间）
  │   → 尾部 min(size,4KB) pread 解析 trailer → DiskChunkSource（pread 拉取式，
  │     本地读内存有界）+ is_temp 本地判定
  │
  └─ TIER2 (远程): streaming cb（DataClientPool::request_raw_exchange +
      NetworkChunkSource）多副本轮换：
      - OBJECT_NOT_FOUND → 删该副本换下一个
      - CHECKSUM → 零容忍预算一次（换副本重取）→ 仍败上抛 CHECKSUM（FATAL）
      - DATA_NOT_READY → 不限期（saw_not_ready 解除 30s deadline）
      - SHUTDOWN → 立即终止；NETWORK → 保留副本退避重试
      - 退避 10ms 起 ×2 上限 500ms ±10% 抖动；轮次尾 TIER3 刷新重进（30s deadline）
      - 副本首查空先 TIER3 刷新再判 NOT_FOUND（master 持有对象场景）
```

export 层把命中的 `ChunkSource + block_area_len` 包成读模式 FlyStream 返回；CHECKSUM → FATAL RuntimeError，NOT_FOUND/NOT_READY → KeyError（对象不可见），网络类全败 → RuntimeError。**禁止整缓冲回退**（#5 裁定，原 NOT_FOUND 回退 `read_object_compressed` 已删）。

Python 消费端（`py/database.py`）：`pickle.Unpickler(stream).load()` 增量消费（内存 R+常数）→ `stream.checksum_failed()` 检查（UnpicklingError/EOFError 等异常同样按损坏）→ 损坏则弃流重开（对象级重来，最多两轮，read_streaming 内已做副本轮换+零容忍预算）→ 仍败 FATAL。成功 → ReadCache populate（`"temp" if stream.is_temp else cache`，记账尺寸 = `stream.total_uncompressed`）。C++ 导出对象（py_name 解析到带 `_read_from_db` 的类）→ 弃流走 `cls._read_from_db`（C++ 权威路径）。

**C++ 全量路径 `read_object_compressed`**：`read_raw_compressed` 三级 fallback 取回完整压缩 record（消费方：C++ `read_object<T>`、backup 副本拉取等非反序列化场景）：

```
read_raw_compressed(name, bypass_local=false)
  │
  ├─ TIER1 (本地): try_read_local_raw（wait_local_write=true：INCOMPLETE 时在
  │   per-db cv 上等本地写完成；FAILED 唤醒后转 TIER2）→ entries 盘读
  │   （temp 与正式统一，temp 无内存态）
  ├─ TIER2 (多副本直连): try_tier2_read 遍历 remote_idx 全部副本
  │   → ReadError 分类重试（见 DataService 节）+ 指数退避 + 30s deadline
  └─ TIER3 (位置查询): remote handler 向 master 查全部副本 → 回填 remote_idx
      → 重入 TIER2（tier3_queried 防弹跳）；master 无位置时返回
      can_still_produce（wait_obj 依赖此信号判断对象是否可能仍被产出）
```

`bypass_local=true`：零容忍重取路径——本地 record 已判定损坏，跳过 TIER1 走远程副本（无副本即失败，不回读坏源）。取回后做 **trailer 零容忍校验**：解析失败 → 失效缓存 + 一次 bypass 重取 → 仍败抛 `DataCorruptionError`（上层转 task 失败）。backup 语义（`backup=true` 且非本地持有）→ `do_backup_write` 写副本记录（源 trailer 损坏则放弃 backup，不落坏数据）。

C++ `read_object<T>` 的 cache 参数三分层：`"none"` 显式 bypass 所有缓存层直读源（读侧每次全新反序列化）；`"low"`（默认）/`"high"` 先查 ObjectCache high 层，miss 走 `read_object_compressed`，反序列化成功且 `checksum_failed()` 为假后 `put_high`。

### 写入时序保证

- **恒流式路径（Python 常规写）**：注册预许可不激活可见性；正式 register 在盘写完成（WBQ finish 单元 future 保证）之后——master 标记就绪时数据已完整可读。
- **`write_object<T>` 路径**：register（可见性激活）先于 WBQ 盘写完成；本地读在 per-db cv 上等 COMPLETE，远程读由 NOT_READY 轮询兜底直至 entry 登记。
- **同一 worker 内连续写入顺序**：`register_write` 是同步往返（发 WriteRegister 后阻塞等 master ack，`WorkerAgent::request_write_register` → `on_write_register_ack`），同一连接串行发起 → 到达 master 的顺序与调用顺序严格一致。默认 `dependency_update_mode==0`（stream 模式）下 master 处理 WriteRegister 时立即 `mark_data_ready` + `update_remote_idx` → **worker 串行写 A→B→C，收到 C 的 ack 即三者均已可见**。
- **推论（read-after-ready 不竞态）**：`@wait_obj` 只需等序列中**最后一个**对象，函数体内读取前置伴随对象是安全的。
- **反模式（曾导致 solver QA flaky）**：`@wait_obj` 等非最后对象、函数体内读其后的伴随对象（如 RAS 收尾 `__ras__sol → __ras__final_res → __ras__iters → __ras__ok` 只等 `__ras__sol` 后读后三个 → EOFError）。修复：等最后的 `__ras__ok`。
- 非 stream 模式（`dependency_update_mode!=0`）：可见性登记延迟到 `on_task_complete` 对 `written_objects_` 统一处理（task 级原子性），不依赖上述「ack 即可见」推论。

---

## 缓存：ReadCache（Python 双池）与 ObjectCache（C++ 单层）

两侧均**不缓存压缩字节**（low-tier 压缩缓存已随 §4.7 取消，读恒走数据源：本地盘 / 远程网络）。

### ReadCache（`py/read_cache.py`，解压 Python 对象）

| 池 | 内容 | 容量 | 说明 |
|----|------|------|------|
| 主池 `_main` | 完整对象，`low`/`high` 等级标记 | `read_cache_size`（默认 1GiB） | 命中查询不分级；**等级只影响淘汰优先级**——low 计分乘 `low_score_factor`（默认 25%，即 0.25）折扣，同热度沉底先淘汰；命中不自动升级 |
| temp 池 `_temp` | temp 对象（`"temp"` 级，池内不分级） | 主池一半 | 路由依据 `is_temp`：本地由 local_idx 判定，TIER2 由 serve META 告知（跨进程读取方本进程查不到 temp 属性） |

- **populate 记账口径**：明文序列化字节（`stream.total_uncompressed`），读侧（消费成功点）与写侧（`write_object(cache!="none")` 预热）一致；temp 写预热恒入 temp 池。
- 淘汰：LFU score = read_count / age；30s 保护期；1.5× 硬限（保护期候选为空且超硬限时全候选）。单对象不设预算上限——超预算照常入池由淘汰兜底。
- 写/删后 `_invalidate_read_cache` 双池清同名，防读后写返回陈旧对象；`FLY_CACHE_GUARD=1` 污染哨兵（populate 快照 hash + 调用栈，命中时对比，检测「读后原地修改污染缓存」）。

### ObjectCache（`cpp/object_cache.h`，反序列化 C++ 对象）

Single-tier LRU（T4 裁定，`low_` 压缩字节池已删）：仅 high 层，存 `CMSharedPtr<T>`（std::any 类型擦除，`get_high<T>` any_cast，类型不匹配视为 miss）。填充点：C++ `read_object<T>` 反序列化成功后（记账 = trailer `total_size_`）；自动失效：duplicate 写 / 落盘失败 / `remove_object` / `abort_task_writes`。淘汰同构（LFU + 30s 保护期 + 1.5× 硬限，`read_cache_size`）。

---

## DataService

单例，管理所有数据的内存索引与读编排。

### 索引与锁

- **local_idx_**：per-db `DbLocalIndex`（objects_ + 共享 `write_cv_`——替代 per-object mutex+cv，避免 88B/对象 × 百万对象的内存爆炸）。`LocalObjectInfo`：db_path + entries（盘读唯一数据定位）+ atomic `completion_state_`（INCOMPLETE/COMPLETE/FAILED，锁外 acquire 读）+ `is_temp_`（temp 判定零 IO）。
- **remote_idx_**：object_name → **副本列表**（`RemoteObjectMeta.workers_` 可多副本），另存 size_bytes_（data locality 调度亲和打分）、accumulated_bytes_/last_suggest_time_（backup suggest）。
- **worker_registry_**：worker 地址 + storage_only/alive 标记（storage_only 优先参与读侧排序）。
- 分片锁：local/worker/db_paths 用 shared_mutex（读并发）；remote 用 **WriterPrefRwLock 写者优先**（防读者被 OS 抢占饿死写者）；cv notify 持锁。

### 读编排

- **三级 fallback**（`read_raw_compressed` 全量路径）与 **read_streaming**（流式路径）的 TIER2 共用同一套 **ReadError 分类重试策略**：

| ReadError | 含义 | 策略 |
|-----------|------|------|
| `NONE` | 成功 | 返回数据 |
| `DATA_NOT_READY` | 对方正在写该对象（瞬时） | 保留副本，无限重试（存在时解除 30s deadline） |
| `OBJECT_NOT_FOUND` | 该副本不再持有此对象（永久） | 删该副本 |
| `NETWORK` | 连接/超时/协议错误（瞬时） | 保留副本，有限重试（30s deadline） |
| `SHUTDOWN` | pool 被停止 | 立即终止 |
| `CHECKSUM` | 校验失败（流式） | 零容忍预算一次（换副本重取）→ 仍败 FATAL |

- 退避：10ms 起 ×2 上限 500ms ±10% 抖动（thread_local mt19937 防请求风暴）。
- **副本遍历顺序**收敛在 `lookup_all_remote_idx` 一处（TIER2 逐副本试读与 TIER3 应答同源）：存活 storage_only 优先 → 存活 hybrid → 已判死副本排尾（避免白费 connect 超时）；同级内按 net_probe 带宽分降序（stable_sort 保注册序，`net_probe_enabled=0` 时不重排）。
- **两层职责正交**：TIER2 负责「读取数据 + 退避重试」，TIER3（`RemoteCompressedReadCallback`）负责「查询位置 + 回填」，查到即重入 TIER2，不取数。master 进程的 TIER3 handler 是纯本地查询（自己是位置权威），保留它是为了无副本时返回 `can_still_produce`。
- handler 由 agent 层注册（master/worker 均注册 streaming + direct + remote 三个，见 `src/agent/cpp/{master,worker}_agent.cpp`）。

### 远程索引更新时机

- WriteRegister 时 master 更新；TIER3 位置查询成功后 worker 回填全部副本。
- **任务分配时预取**：`on_task_assign` 收到 `TaskAssignMessage.dependency_locations_` 时直接全部回填 remote_idx，使首轮读命中 TIER2 而非落 TIER3。

### 其他

- **分片位置查询** `find_chunked_location`：返回对象最近 entry 的落盘定位（绝对路径 + 区间），DataServer 分片路径与本地 DiskChunkSource 共用；COMPLETE 才返回，temp 一视同仁（temp 恒有盘 entry）。
- **auto-backup 访问追踪**：TIER2 读命中（含流式）后 `record_remote_access` + `maybe_suggest_backup`（增量阈值 + cooldown）。
- **DB 迁移重定向**：`_MIGRATED_TO` 机制已废弃（db chain 取代，`py/database.py` + `py/db_meta.py`），旧残留兼容读取。
- **auto-backup / 存储接管 / 自动补齐存储节点**等集群机制由 agent 层驱动（配置键见下表），storage 侧提供 `get_worker_bytes_batch` / `get_objects_of_worker` 等查询。

---

## DataServer

### 架构

epoll + send 线程池（`data_server_threads`，默认 4，对半分 epoll/send）。epoll 线程收帧按消息 type dispatch，发送任务入 SendTask 队列由 send 线程消费；EV_ONESHOT + 发送完成后 rearm。帧头 check 位失配 = 流失步，直接断连。

### 消息 dispatch（数据面）

| 消息 | 方向 | 处理 |
|------|------|------|
| `DATA_REQUEST`(11) | client → server | 分流快路径 / 分片路径（见下） |
| `NET_PROBE_REQUEST`(40) | client → server | 按请求 payload_size 生成 dummy payload 回 NetProbeResponse（对端 BandwidthProbeThread 测带宽） |
| `CHUNK_RESEND`(64) | client → server | 单片重传（见分片路径） |

（`DATA_CHUNK`(63) / `DATA_DIGEST`(65) 是 server → client 的流内帧，不走 dispatch。）

### 整帧快路径（record ≤ `chunked_transfer_threshold`，默认 4MB，或无分片定位）

`try_read_local_raw(wait_local_write=false)`——serve IO 线程池不能阻塞 wait，INCOMPLETE 立即返回 NOT_READY 让对端轮询。取到的 record 先过 `DecompressingStreamBuf` 校验：**本地 record 损坏不服务坏数据**，回 ERROR 让 client TIER2 换副本（本地对象坏 ≠ 连接坏）。响应携带 py_name / write_context_hash / **is_temp**（缓存双池路由，远端读取方查不到本地属性）/ `payload_crc_` 根摘要（client 收满校验）。两段式编码：小 fields 走 bitsery，raw payload 以 shared_ptr 引用零拷贝；发送用 **writev scatter-gather**（header + payload 一次 sendv）。

### L2 分片路径（`serve_chunked`，权威设计见 chunked-transfer-design §4.5）

分流条件：本地完整落盘（`find_chunked_location` 命中）且 `loc.size > chunked_transfer_threshold`。

```
META（复用 DataResponseMessage 两段式，无 raw）
  chunked_=true, total_compressed_len_, chunk_frame_bytes_=4MB,
  py_name_/trailer_len_（发送前尾部 pread min(size,4KB) 解析 trailer——
  流式消费端无法预先读流尾）, chunk_compression_type_, is_temp_
  ↓
CHUNK 帧循环（4MB 切帧，纯字节切片，与磁盘块结构无关）
  帧头（29B）单独 send + payload sendfile 零拷贝（f38488f，原 writev 整帧已改）
  ——file→socket 内核直通，serve 端不再持有 4MB 单片缓冲、不碰数据字节；
  帧片 CRC 发 0（§14.8 裁定取消计算），块级 CRC 由解压出口权威校验
  ↓
DIGEST 尾帧（root_crc_=0 = 未计算——T5 根摘要双侧消除，L0 块级 CRC + trailer
  已承担完整性，整 record 单遍根摘要是冗余遍历；帧本身保留作 client 的
  流结束标记，完整性对账靠字节计数）
```

客户端两条重组路径对 `root_crc_==0` 一律跳过复核：NetworkChunkSource 侧兼容验证已删（0529d7b）；**DataClientPool 侧「非 0 照验」保留**（防御深度——DCP 是远端读取主路径，该分支有 fake-serve 注入测试证明其防护价值，见 `data_client_pool.cpp` 注释），与 NCS 的不对称是刻意差异，非遗漏。

**CHUNK_RESEND**：byte-offset 寻址（offset 相对 record 起点，server 零块知识）——conn 状态登记对象区间，**每区间上限一次**（重复请求 = 协议异常，断连防御）；重传 = pread + sendv 纯字节区间（帧片 CRC 同样发 0），client 按字节替换 hole 后块级解析驱动校验。

### stop() 与 lost wakeup 唤醒纪律

`stop()` 置 `running_=false` 后**必须持 `send_mutex_` 再 `notify_all()`**（8419526）：send_loop 的「持锁查谓词 → 释放锁 wait」序列下，不持锁 notify 可能落在查谓词窗口 → notify 落空 → 永久 wait → join hang。持锁 notify 保证要么 waiter 已 wait（被唤醒），要么查谓词时看到 `running_=false` 直接退出。这是 condition variable 通用纪律：notify 必须与修改共享状态的代码在同一 mutex 保护下。

---

## 配置键（storage 相关，`src/core/cpp/config.cpp`）

| 键 | 默认 | 说明 |
|----|------|------|
| `compression_type` | `"lz4"` | 压缩算法（lz4/zlib/zstd/none） |
| `compression_level` | 0 | 压缩级别 |
| `compression_threshold` | 4096 | 流级 raw 阈值：尾块 ≤ 此字节数整块 raw 直通（小对象免压缩开销） |
| `serialize_chunk_size` | 4194304 (4MB) | 管线切块大小（远小于 wire 块头 64MB 上界） |
| `aggregation_threshold` | 1048576 (1MB) | DataWriter 滚文件阈值（增量写过半即滚，record 不跨文件） |
| `chunked_transfer_threshold` | 4194304 (4MB) | DataServer 分片路径分流阈值（record 超过走 META+CHUNK 流，否则整帧快路径） |
| `stream_buffer_chunks` | 16 | NetworkChunkSource 有界队列上限（片数，≈64MB 压缩态） |
| `read_cache_size` | 1073741824 (1GiB) | ReadCache 主池 / ObjectCache 字节预算（temp 池为主池一半） |
| `low_score_factor` | 25 | low 等级计分折扣（百分比：25 = low 分数为 high 的 25%，同热度淘汰沉底） |
| `data_server_threads` | 4 | DataServer epoll+send 总线程数（对半分） |
| `data_client_pool_size` | 4 | 远程读连接池 slot 数 |
| `net_probe_enabled` / `net_probe_interval_ms` / `net_probe_payload_kb` / `net_probe_timeout_ms` | 1 / 30000 / 256 / 3000 | 带宽探测（读侧副本按网络质量排序） |
| `locality_scheduling_enabled` | 1 | data locality 调度：按 remote_idx size_bytes_ 亲和打分 |
| `auto_backup_enabled` 及 `worker_suggest_*` / `backup_*` / `master_ewma_decay_per_sec` | 见 config.cpp | auto-backup 双层机制（worker suggest + master EWMA 聚合） |
| `storage_takeover_enabled` / `storage_takeover_fail_timeout` / `storage_takeover_max_writers` | 0 / 60 / 64 | 判死后同 host storage_only 只读接管死 worker idx |
| `auto_storage_nodes_enabled` / `auto_storage_check_interval` | 0 / 30 | 自动补齐 storage_only 节点（opt-in） |
| `dependency_update_mode` | 0 | 0=stream（WriteRegister 即时可见）；≠0 task 级原子可见 |

**已删除键**（代码零残留，仅注释留档）：`temp_store_size`（temp LRU/eviction 退役，config.cpp:175 注释）、`streaming_read_threshold` / `streaming_write_threshold`（恒流式裁定，开关与逃生口删除）。

---

## 设计决策

| 决策 | 原因 |
|------|------|
| 恒流式读写（无阈值开关） | GB 级对象内存 R+常数；开关与双路径的维护成本 > 收益（2026-08-30/31 裁定） |
| Stage 插件管线 | 磁盘/数据面 RPC/PeerRpc 三端共用同一对 sink/source 抽象；变换逻辑可独立增删，字节格式 golden 锚定 |
| 块级 85% 规则 + raw 直通 | float64 等高熵数据压缩是纯负优化；`comp == unc` 隐式标记零开销 |
| 16B wire 块头 + 64MB 上界校验 | 位翻转/坏流 garbage 不再转化为未捕获 bad_alloc；CRC 验证在 resize 之后挡不住 |
| temp 恒落盘 + temp writer | task 级断点：已完成 task 的 temp 输出跨进程可恢复；内存 LRU/eviction 机制整体退役 |
| 读缓存双池（Python）+ 单层（C++） | 压缩字节缓存零生产消费（读恒走数据源）；完整对象缓存的 low/high 语义降为淘汰优先级 |
| 三级 fallback + 多副本轮询 + 分类重试 | 本地优先；ReadError 区分瞬时/永久错误决定保留/删除副本 |
| 分片 serve：帧头 send + sendfile | serve 端不碰数据字节、不持 4MB 缓冲，file→socket 内核直通（实测见 performance-analysis） |
| 帧片 CRC 发 0 + 块级 CRC 解压出口权威校验 | sendfile 路径 server 不读数据无法廉价算 CRC；块级 CRC 在写入时刻锚定，覆盖全生命周期 |
| DIGEST root_crc 发 0（DCP 非 0 照验保留） | L0 块 CRC + trailer 已承担完整性，根摘要是冗余遍历；DCP 分支作防御深度 |
| 注册时序（恒流式：盘写完成后正式 register） | master 标记就绪时数据已完整可读；`<T>` 路径注册先行由 NOT_READY 轮询兜底 |
| WriteRegister 同步往返 ack | 同 worker 连续写的全局可见顺序 = 调用顺序，read-after-ready 不竞态 |
| 持锁 notify（DataServer stop 等） | 消灭 cv lost wakeup；notify 必须与共享状态写入同锁 |

## 交叉引用

- [`docs/chunked-transfer-design.md`](../chunked-transfer-design.md) — 分片/流式传输权威设计（L0-L3 分层、§14 v2 各项裁定：注册预许可、B' 块表、A' 重传、缓存取消、恒流式、temp 去①形态、双池、执行上提）
- [`docs/rpc-stream-pipeline.md`](../rpc-stream-pipeline.md) — PeerRpc 流式大 payload：复用同一压缩块管线（sink/source 抽象第 3 步装配）
- [`docs/performance-analysis-2026-08-31.md`](../performance-analysis-2026-08-31.md) — sendfile/零拷贝读链优化实测（1218 MB/s goodput 基线）
- [`docs/issues/001-failed-task-rerun-write-duplication.md`](../issues/001-failed-task-rerun-write-duplication.md) — 写入事务与 task 失败脏数据清理
- [`docs/python-api/module.md`](../python-api/module.md) — Python 公开 API 权威总表
