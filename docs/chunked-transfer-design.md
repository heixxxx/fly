# 大对象分块传输与数据完整性——设计定稿与实施计划

> 状态：**设计定稿 + 实施计划定稿**。L0（前置层）待批准即开工；L2/L3/L1 依次排队；L4 绑定亿级触发。
> 日期：2026-08-28 调研 / 2026-08-29 定稿（合并原 chunked-transfer-design.md 与 frame-integrity-impl-plan.md）。
> 关联：[emir-capability-gap.md](emir-capability-gap.md) P0-3（差距条目权威落点）、[remaining-todo.md](remaining-todo.md) F4（⏸ 降级）。
> 性能依据（实测，Ryzen 7735H）：ISA-L `crc64_ecma_refl` **14.6 GB/s**（比软件 slicing-by-8 快 7×、比硬件 CRC32C 快 1.5×，seed 链式增量已验证）；4MB 块粒度下逐块/链式/组合式校验与整块吞吐 **±2% 等价**——校验粒度是结构选择，不是性能权衡。

## 1. 背景与定位

P0-3 调研结论：credit 窗口流控**裁定不做**（fly 内网批处理负载画像中不存在"持续速率失衡"问题）；背压已被结构性覆盖（每连接单 in-flight + slot 信号量 + oneshot rearm）；**分块的核心价值是内存有界化而非网络公平**——当前整取链在 GB 级对象上先撞内存墙（4 并发 × ≈3×对象压缩后大小 ≈ 16GB 峰值）而非带宽墙。

工业零容忍（用户裁定）：fly 面向工业流程，计算结果完全依赖数据正确性，**错误数据 0 容忍**——校验错误一次重取，仍败即 FATAL + task 失败。

## 2. 现状链路与三个整块边界

### 2.1 已有分块资产（改造面比 P0-3 原设想小）

- **磁盘格式天然分块**：`CompressingStreamBuf` 产出 `[Chunk1..N]`，每块 `[i32 unc][i32 comp][data]`，默认 4MB（`serialize_chunk_size`），**每块独立压缩**，块边界自寻址。
- **按需解压已就绪**：`DecompressingStreamBuf` 是 streambuf 接口的按需解压器，为流式反序列化设计。
- **流式序列化已就绪（写侧）**：`FlyStream` 写模式 + `pickle.dump(obj, stream)`——无整 pickled bytes。
- **流式读原语已存在但未被使用**：`FlyStream` 读模式已导出 Python；`_read_streaming` 名不副实（整块返回）——现成接入缝。
- **索引已含对象级寻址**：`IndexEntry(file_name_, offset_, size_)`；对象在 .dat 段文件连续。
- **WriteBackQueue 本就是闭包单元**（`WriteRequest{execute_,on_complete_,on_error_}`）+ high_watermark=10 背压（超限阻塞到清空）——「压缩一块、入队一块」的现成挂点。
- **段事务 API 现成**：mark_begin（记回滚点）/ mark_end / abort_segment；写一半崩溃 → load_db 丢弃未闭合段。
- **等待语义现成**：本地读 `wait_local_write` per-db cv；远程 `is_write_in_progress` → NOT_READY 轮询。

### 2.2 三个整块边界（内存压力全部来源）

| # | 边界 | 代码位置 | 峰值驻留 |
|---|------|---------|---------|
| 1 | **写路径**：FlyStream dest 为内存 FlyBuffer 整累积；`commit_stream` 剥 nanobind deleter 再整拷贝 | fly_stream.h:37、database.cpp:307-335 | R + 2C |
| 2 | **网络**：server `try_read_local_raw` 整读 C + `send_queue_` pin 至 writev 完成；client `recv_exact` 整收 | data_server.cpp:282、data_client_pool.cpp:321-324 | 两端各 C |
| 3 | **读路径**：`_read_decompressed` 整解压 + `pickle.loads` 整反序列化 | database.py:153-175 | C + 2R |

（R=原始大小，C=压缩后大小；C 还被 ObjectCache low-tier（`read_cache_size`，默认 1GB 预算）pin 住。）

## 3. 分层方案总览

```
L0 前置层（原 frame-integrity 范围）── 帧头 uint64 + 校验层 + trailer 格式 + 零容忍语义
L1 写路径流式 ──── 压缩流逐块直入 WBQ 落盘（纯追加）
L2 网络分片传输 ── META + CHUNK 帧流 + 在线块重传
L3 读侧流式 v2 ─── 接收线程 + 有界队列 + Unpickler 增量反序列化（真并行）
L4 块级寻址 ────── 部分读/块粒度缓存（远期，绑定亿级触发）
实施顺序：L0（§6）→ L2（§7）→ L3（§8）→ L1（§9）→ L4 等触发
```

## 4. 线格式与协议

### 4.1 帧头（所有消息类型统一，L0）

```
现: [4B total_len BE][1B type][payload ...]           total_len = 1 + payload (uint32)
新: [8B header   BE][1B type][payload ...]           帧前缀 5B → 9B，type 移至 offset 8
    header = (check << 48) | len
    len    = 1 + payload_size                          48 位，上限 256TB（无尺寸政策）
    check  = 0xF17E ^ fold16(len)
    fold16(len) = (len ^ (len >> 16) ^ (len >> 32)) & 0xFFFF
```

性质：长度域任一单比特翻转 → fold 确定性变化（每位唯一映射到一个保留 fold 位，无抵消）；失步/垃圾 8 字节误过概率 2⁻¹⁶；`len = 0` 非法。消除 uint32 截断静默回绕（4GiB）与 client 侧 256MB 假上限。

### 4.2 DATA_RESPONSE 两段式（子头不变，全 uint64 运算，L0）

```
[8B 帧头][1B type=DATA_RESPONSE][4B small_fields_len][1B has_raw][small fields][raw ...]
total_len = 1 + 4 + 1 + small_fields_len + raw_len     (uint64)
raw_len   = total_len - 6 - small_fields_len            (uint64)
small_fields_len 保持 uint32（bitsery 小字段，非文件尺寸）
```

整帧快路径（小对象/缓存命中）携带 `payload_crc_`（wire 根摘要，§4.5 三层校验之一）。

### 4.3 校验包装层（L0）

`src/common/cpp/data_checksum.h/.cpp`——**稳定函数接口**（用户裁定：后续更优校验方案只改实现、接口零变化）：

```cpp
namespace fly {
uint64_t data_checksum(const char* data, size_t len);   // 整块
class DataChecksum {                                    // 增量（== 整块任意切分）
public: void update(const char* data, size_t len);
         uint64_t final();
};
}
```

契约（任何未来实现必须满足，头文件注释锚定 + 契约测试）：① 64 位摘要；② 增量 == 整块；③ 确定性（空输入 `final()` 为固定初值）；④ 随机损坏误过 ≤ 2⁻³²。当前实现 = ISA-L `crc64_ecma_refl`（init=0 链式 seed 语义；`final()` 即链式状态）。**`<isa-l/crc64.h>` 只出现在 `data_checksum.cpp` 一处**——换实现改一个文件。ISA-L 集成：`third_party/BUILD` 增 `cc_library(name="isal", linkopts=["-lisal"])`（同 lz4/zstd 系统库模式）；部署依赖 +libisal2。

### 4.4 磁盘 record 格式（trailer 化 + 块 CRC，L0，无版本兼容负担）

```
现: [ObjectHeader][Chunk1][Chunk2]...   每块 [i32 unc][i32 comp][data]
新: [Chunk1][Chunk2]...[ChunkN][ObjectHeader]   每块 [i32 unc][i32 comp][u64 crc][data]
                                     └─ trailer：magic/版本/py_name/total_size/chunk_count/压缩类型
                                       每块 crc = data_checksum(压缩后字节)，块头 8B→16B（开销 0.0002%）
```

- **header trailer 化（用户裁定，取代"占位+seek 回写"）**：流式写全程纯追加（`ios::app` 下 seekp 本就无法重定向写入，回滚重开句柄即 app 模式）；total_size/chunk_count 写完末块自然已知；**trailer 兼作 commit marker**——崩溃残块无 trailer，结构上不可误读为完整对象（WAL commit record / Parquet footer 同款）。
- **idx 零变更**：`IndexEntry.offset_+size_` 即起止区间；读取方先读区间尾部（fixed_header_size → py_name_len → trailer 全长）解析，再从区间起点流式走 chunk；chunk 走读必须恰好消耗 `size − trailer_size`，越界即结构损坏（增强校验）。
- 压缩块与 raw passthrough 小对象块同格式；内存路径同步受益（compress_buffered_data 的占位+memcpy 覆盖消失）。
- 写侧唯一入口：`CompressingStreamBuf::flush_chunk`（FlyStream 与 compress_buffered_data 共用）+ 完成时 trailer 追加；读侧唯一入口：`DecompressingStreamBuf`（构造改尾部解析，约 5 处消费点联动：decompress_raw_data / FlyStream 读模式 / DataServer py_name 解析 / low-tier 命中重解析 / _read_streaming）。

### 4.5 分片传输协议（L2）

```
client                          server
  │ DATA_REQUEST (object_name) ──▶
  │                              查 IndexEntry → (file, offset, size)；
  │                              pread 尾部 trailer → py_name/total/chunk_count
  │ ◀── DATA_RESPONSE_META ────── status / py_name / write_context_hash /
  │                              total_compressed_len (uint64) / chunk_count / chunked=1
  │ ◀── DATA_CHUNK #0 ─────────── [seq][u64 帧CRC][bytes]（每帧一片，4MB）
  │ ◀── DATA_CHUNK #1 ...
  │ ◀── DATA_DIGEST ───────────── 根摘要（server 边发边算，单遍）+ chunk_count 复核
  │ ── CHUNK_RESEND(seq) ──────── （仅当帧 CRC 验坏；流内或流后，同连接，每块上限一次）
  │   （收满 total_compressed_len 且根摘要过验即完成）
```

- **根摘要放流尾 DIGEST 帧**（server 单遍：边 pread 边发边算根，无需预扫）——与磁盘 trailer 尾置同构。
- **三层校验分工**：帧头 check 位（失步/垃圾）→ **CHUNK 帧 CRC**（传输跳，接收侧验证，驱动在线块重传）→ **磁盘内嵌块 CRC**（写入时刻锚点，解压时验证，覆盖全生命周期：磁盘 → server 内存 → 网络 → client 内存 → 解压）；DIGEST 根摘要做端到端绑定（乱序/调包兜底）。
- **小对象快路径**：total_compressed_len ≤ 单片阈值 → 现整帧两段式（含 `payload_crc_`），一次往返。（~~或缓存命中~~ 2026-08-29 废止：low-tier cache 全量取消，见 §4.7——分流只看尺寸。）
- **CHUNK_RESEND 处理模型**：client 验坏块后发 resend 请求（同连接），server 读循环路由该消息类型单块重发；每块重传上限一次，再失败升格对象级 FATAL。与「不做断线续传」不冲突：**在线块重传**（连接活着）做；**断线**仍整对象走 TIER2 重试。

**L2 落地修订（2026-08-29 实现）**：
- CHUNK 片 = **纯字节切片**（4MB 固定，与磁盘块结构无关）——client 顺序重组后与磁盘 record 字节一致，DecompressingStreamBuf 直接消费（磁盘块 CRC 语义完整保留）；重传单位 = 字节片。
- META（DATA_RESPONSE 复用）增 `chunk_frame_bytes_`（切片尺寸随 META 告知——client 按 seq×frame 定位填充，切片尺寸是发送端实现细节）。
- `chunked_transfer_threshold` config（默认 4MB）：record 超过阈值走分片，否则整帧快路径（缓存命中/temp 对象天然走快路径——find_chunked_location 只定位落盘 entry）。
- L3 前置约束（流式尾部解析冲突的解）：**META 将携带 trailer 元数据**（server 发送前 pread 尾部一次解析 py_name/trailer_len 填入 META）——消费端流式启动时即知块流边界；L2 阶段 client 重组后自行尾部解析的行为在 L3 由 META 取代。

**L3 落地修订（2026-08-29 实现）**：
- ChunkSource 抽象落 common（纯虚接口；MemoryChunkSource 留 storage——依赖 ObjectHeader）。DecompressingStreamBuf 双构造（内存/源驱动）。
- META 增补 `trailer_len_` + `chunk_compression_type_`（流式解压器选择——类型在 trailer，流尾不可预读）。
- **TIER2 流式 = 首副本 best-effort**（streaming cb 只试 lookup 排序首位）：失败/流中途异常 → 回退 `read_object_compressed` 完整编排（副本轮换/退避/零容忍语义零重复实现）。流式中断续传的 TIER2 编排留待后续。
- read_streaming 的 TIER1 = `SharedMemoryChunkSource`（持有 FlyBufferPtr 所有权）。
- Python 面：`_fly_storage.ex_stg_open_read_stream` 模块级工厂（nanobind 类方法返回 unique_ptr 不被支持——模块函数有先例）；read_object 的 pickle "low"/"none" 分支 → Unpickler(FlyStream) 增量消费；流中途异常/校验失败 → 回退 `_read_decompressed`（完整重取 + FATAL）。
- `streaming_read_threshold`（默认 64MB）实际语义为**功能开关**（读前不知对象大小，探 size 需额外 IO 得不偿失）；`stream_buffer_chunks`（默认 16）为接收线程有界队列上限。

**L1 落地修订（2026-08-29 实现）**：
- DataWriter 增量 API：begin（文件过半先滚——增量写不跨文件，IndexEntry 单文件区间约束）/append（手工跟踪大小，app 模式 tellp 受限）/finish（entry 登记；trailer 由调用方经 append 写入）。
- FlyStream sink 写模式：压缩块逐块回调（"压缩一块、交付一块"）；finish_sink 时 trailer 走 sink 并暴露元数据。
- `Database::open_write_stream` → WriteStreamHandle：commit 对齐 commit_write 时序（段事务 BEGIN / register / record_func 同步 / 完成登记——盘写已完成故同步收尾）。
- **盘写在任务线程同步进行**（write 进 page cache 即返回——与 WBQ 后台 execute 延迟特征一致；WBQ 逐块后台化留作后续优化）。DUPLICATE 预检未前移（register 时序安全已在 §9.4 验证，大对象 DUPLICATE 罕见）。
- `streaming_write_threshold`（默认 1=启用）：写前不知对象大小——开关启用即统一走流式（小对象走增量 API 等价）。populate_cache 语义：流式写路径不 populate low-tier（§9.4 大对象分档）。

### 4.6 统一块模型（2026-08-29 用户构想对齐，v2 设计定稿）

> 背景：实现与用户原始构想逐条对照（§4.5 落地修订的"纯字节切片"方案）后收敛的统一模型。
> 核心原则（用户裁定）：**正常传输路径双方零块感知；块结构知识只存在于 client
> 校验侧与 trailer 元信息中**。校验单位 = 传输数据自然单位（磁盘压缩块），
> 重传单位 = 最小损坏单位（单块字节区间）。

**用户构想六条（原意存档）**：
1. 流式序列化+压缩：缓冲填满→压缩+块 CRC→**入后台线程落盘**（WBQ）；块追加 data 文件；块元信息入 trailer
2. 磁盘结构：块位置信息记录在 data 文件尾部 trailer（**不进 idx**——随 record 跨机自包含）
3. 远程读：service 经 idx 拿对象区间，数据直接发送——**不关心块结构**（纯字节管道）
4. client：网络 io 接收侧**每收到一个块先 CRC 校验**，无误推入反序列化流；失败则发请求告知需要的字节区间，server 重传该块
5. 重传期间：网络 io 不向反序列化流供数（hole 前按序、hole 处暂停），后续数据入缓存；缓存满则暂停接收（TCP 流控自动压 server —— 自动流控）
6. 重传块校验通过后，继续供给反序列化流

**统一数据流**：
```
写：序列化流 → 4MB 缓冲满 → 压缩+块CRC → 入 WBQ 后台落盘（追加 data）
    完成时 trailer 追加：[块位置表][py_name][fixed(含块数/表长)][trailer CRC]

正常远程读：
  server：idx 拿区间 → 预读尾部 trailer（填 META）→ 区间字节流连续发送
  client 接收线程（纯 C++ 无 GIL）：收字节 → 自行按块边界切分
          （解析 16B 块头取 comp 长度）→ 逐块验块 CRC → 好块入有界队列
  消费线程：解压（块头已验，CRC 复验为双保险）→ Unpickler 反序列化

坏块重传：
  接收线程发现块 K CRC 错 → 不入队（hole），后续字节继续收进 pending
  → CHUNK_RESEND(byte offset, length) → server pread 该区间重发
  （server 全程零块知识——offset/len 即区间寻址，无须块表）
  → client 验证通过 → 填洞 → 按序继续供给
  （pending 满 → 接收停 → TCP 流控压 server）
```

**与 §4.4/§4.5 落地现状的差距（三项待实施）**：

| 项 | 内容 | 磁盘/协议变更 |
|---|---|---|
| B' | trailer 增**块位置表**（变长，位于 py_name 之前；fixed 增 `block_table_len`）；写侧 finish 时登记（每块 `u32 comp_len` 紧凑式，前缀和即得偏移） | 磁盘格式 |
| A' | client 接收线程**块级 CRC 校验前移**（从解压器移到网络 io 层）；坏块 → resend(byte offset, len)；CHUNK 帧头**去帧级 CRC、保留帧头 check 位**（失步检测一层 + 数据校验一层，职责干净） | 传输协议 |
| C | 写路径 sink 逐块入 **WBQ 后台落盘** + 完成单元（trailer+entry 在完成单元；register 时序不变） | 写路径 |

**三细节裁定（2026-08-29 用户同意）**：
1. resend 寻址 = **byte offset + length**（server 零块知识；天然适配任何块大小/config 变化——client 解析过块头即知坏块区间）
2. **帧级 CRC 去除**，保留帧头 check 位（帧头自身损坏的兜底）
3. 块表条目 = **u32 comp_len 紧凑式**（表受 trailer CRC 保护；L4 需要偏移时前缀和一遍即得）

**块表的消费者**（resend 不需要它——offset/len 寻址）：client 结构对账强化
（逐块累计 == 块区总长，防块头域损坏导致的边界漂移）；L4 部分读地基；
跨机自包含（随 record 复制走，无需 idx 同步）。

**实施顺序（TDD）**：B'（磁盘格式 + 写侧登记 + 读侧对账）→ A'（接收线程
块校验 + resend offset 化 + 帧简化）→ C（WBQ 后台落盘）→ 全量 QA +
100 轮稳定性。

**§12 不做清单同步修订**：原"写入时块索引落盘（懒构建够用，除非 L4）"
一条废止——块位置表以 trailer 内嵌形式纳入本设计（B'）。

**L3 TIER2 完整流式编排（2026-08-29 用户裁定，差异讨论 #5 定案）**：
- 流式路径**自备 TIER2 重试**：传到一半流断（对端连接断开数据未传完）→
  本次流式失败 → 按 TIER2 规则选下一副本**重新流式**（重新开流 + 重新消费，
  对象级重来——无断点续传、消费端数据不可回卷）；副本轮换 + 指数退避 +
  30s deadline + TIER3 刷新，与整缓冲 TIER2 同构。
- **禁止整缓冲回退（用户明确："始终不使用整体接收的方式"）**——现有
  read_streaming 的"首副本 best-effort + 失败回退 read_object_compressed"
  逻辑移除。
- 消费失败分类：网络类（断连/超时）→ 轮换下一副本重新流式；校验类
  （块 CRC/trailer/DIGEST）→ 零容忍预算（§5：一次重取）→ 仍败 FATAL。
- 实施位置：export 层流式编排（消费入口可重入——每次重试重新
  Unpickler(新流)）；与 A' 同批次。流式重试中 remove_remote_location
  使死副本出序。
- 范围界定：该裁定针对**流式读路径**；L2 整缓冲重组（receive_chunked）
  保留给非流式读（小对象快路径 / streaming_read_threshold=0 逃生口）。

**threshold 语义裁定（2026-08-29 用户裁定，差异讨论 #6 定案）**：
- `streaming_read/write_threshold` 定性为**开关**（>0 启用，值无意义，
  保留原名不改）；**读/写统一流式，不按对象尺寸分档**（用户裁定：统一
  流式易于维护）。
- 维度澄清：分档只存在于**传输层**（chunked_transfer_threshold：小对象
  ≤4MB 整帧快路径 / 大对象分片流——这是 server 侧内存有界化所需）；
  **消费层统一流式**（快路径整帧收到后同样以内存源流式消费）。

**META 字段裁定（2026-08-29 用户同意，差异讨论 #3 定案）**：
- `trailer_len_` + `chunk_compression_type_` **保留**（流式消费"流首启动、
  流尾未知"的刚需——凡 trailer 来的信息必须经 META 预告知；B' 后 trailer_len
  语义扩展覆盖块表）。
- `chunk_frame_bytes_` **随 A' 实施退役**（块自寻址后无消费者——L2 整缓冲
  重组路径一并块级化）。
- **保持 DATA_RESPONSE 复用**，不拆独立 META 消息类型（快路径零值字段
  ~4 字节实测可忽略；一个对象一次交互的消息类型数量最小化；用户同意）。

### 4.7 low-tier cache 全量取消（2026-08-29 用户裁定，差异讨论 #4 定案）

> 裁定原文（用户）：考虑到 low level cache 较难处理，且对内存影响较大，
> 直接全量取消 low level cache 行为，远程读统一走磁盘——实测磁盘 IO 基本
> 隐藏在网络 IO 后，low cache 基本没有太大意义。
> 澄清（同日）：取消的含义是**缓存模式二值化——之后只有"无缓存"和
> "high level 缓存"两种**。

- **范围**：仅 low-tier（压缩字节缓存，ObjectCache put_low/get_low）。
  high-tier 不动（C++ 解压对象缓存 / Python ReadCache 是反序列化层，另一层抽象）。
- **取消后语义**：
  - 缓存模式二值化：`"none"`（无缓存，恒走盘）/ `"high"`（解压对象缓存）。
    `"low"` 从语义上消失——API 兼容期作为 `"none"` 别名（read_object 的
    默认值改为 "none"）。
  - 远程 serve / 本地读：恒走盘（本地写后立即读由 `wait_local_write` cv
    等待兜底；远程写后立即读由 NOT_READY 轮询兜底——均为现成语义）。
  - **顺带根治 §4.6 讨论的"未落盘大对象整帧 pin C"残余路径**：不再
    populate 后，该场景自动变 NOT_READY → 落盘 → 分片，server 内存恒常数
    （无须 serve_chunked 内存源版本）。
  - **连锁配套（必须）**：L3 TIER1 本地路径原依赖缓存共享（SharedMemoryChunkSource
    零拷贝）——取消后本地流式读退化为整读盘进内存，需补 **DiskChunkSource**
    （pread 拉取式磁盘源，与 NetworkChunkSource 同构，复用块级校验），
    归入 B'/A' 实施批次。
  - 内存：释放 read_cache_size 预算（默认 1GB）。
- **联动清理**（实施清单）：commit_write/read_object_compressed/
  try_read_local_raw 三处缓存分支移除；write 的 populate_cache 参数与
  read 的 cache 参数按二值化语义处理；read_cache_size 闲置；QA read_cache
  系列三例 + cpp_object_cache 的 low 断言改写为盘路径验证；ObjectCache
  low-tier 结构物理删除作独立后续清理（先行为取消后结构删除，风险分步）。
- **同步修订 §4.5**："缓存命中→整帧快路径"条款废止——快路径分流只看
  尺寸阈值：小对象（≤阈值）整帧；大对象恒分片（落盘 pread；未落盘场景
  由 NOT_READY 收敛到落盘后分片）。

## 5. 失败与重试语义（工业零容忍，对象级统一）

| 失败类别 | 语义 |
|---------|------|
| 网络类（断连/超时） | 维持现状：TIER2 副本轮换 + 指数退避 + 30s deadline |
| **校验类（帧头 check / CHUNK 帧 CRC / 磁盘块 CRC / DIGEST 根，任一来源）** | `[FATAL-DATA-CORRUPTION]` 级 ERR 日志（对象/来源/期望与实测校验值）→ 失效 low-tier 缓存条目（缓存副本可能是坏源）→ **对象级一次重取**：有远程副本 → TIER2 重取（下一副本优先，无副本可用重发同副本）；仅本地 → 绕过缓存重读盘 → 重取唯一可接受结果 = 干净通过全部校验；仍旧校验错误**或以任何方式失败**（断连/超时/NOT_FOUND）→ **FATAL**：`RuntimeError("[FATAL-DATA-CORRUPTION] ...")` 上抛 → 既有 task 失败通道（TaskFailedMessage）——**task 失败结束，worker 不崩溃** |

不做静默重试循环、不做跨副本校验错误轮询——持续校验失败意味着内存/硬件/代码缺陷，必须大声暴露。误放过概率：CRC-64 ≈ 2⁻⁶⁴/次 + 解压结构校验 + 帧头 check——残余风险可忽略且可量化。DATA_NOT_READY 语义不变。本地重读盘放大一次读 IO——仅校验失败路径发生。

## 6. L0 前置层实施计划（六步 TDD，待批准即开工）

### 6.1 改动点

**帧头（7 处代码 + 3 处测试）**

| # | 文件:位置 | 改动 |
|---|----------|------|
| 1 | `src/network/cpp/message_protocol.h` | `read_be64/write_be64`、`FRAME_LEN_MASK/FRAME_CHECK_MAGIC/frame_check_bits/make_frame_header/parse_frame_header`；`MessageProtocol::encode/decode/get_type/get_total_size/get_payload_size` 换 9B 前缀 + uint64；`DataResponseProtocol::encode` 去 uint32 截断；`raw_len_from_total` → uint64 |
| 2 | `src/network/cpp/data_client_pool.cpp:264-330` | 9B 帧头；uint64；**删除 256MB 上限**（check 位 + `len>=6` 下界接管）；`resize` 包 try/catch（bad_alloc → NETWORK + 明确报错） |
| 3 | `src/network/cpp/metadata_client.cpp:54-88` | 9B 帧头；uint64；保留 16MB 域上限（元数据域内界，注释注明）；`full_buf` 重建 `8+len` |
| 4 | `src/storage/cpp/data_server.cpp:226-238` | 切帧循环 `>=9` + `parse_frame_header`（非法 → ERR + 断开）；`frame_size = 8 + total_len` |
| 5 | `src/agent/cpp/worker_agent.cpp:763-776` | probe 响应读 9B 头；remain uint64 |
| 6 | `src/network/cpp/reactor.cpp:267-279` | 两处 uint64 + `8+total_size` |
| 7 | `src/agent/cpp/peer_rpc_server.cpp:102-130` | `>=9`、`buf[8]`、`8+total_len` |
| T1 | `src/network/tests/message_protocol_test.cpp` | 新增测试 1-4（§6.2） |
| T2 | `src/network/tests/data_transfer_test.cpp:197-224` | fake client 读头 5B→9B |
| T3 | `src/network/tests/metadata_client_test.cpp:64-88` | 同上 |

**校验层（5 处）**

| # | 文件:位置 | 改动 |
|---|----------|------|
| 8 | `src/common/cpp/data_checksum.h/.cpp`（新建）+ `third_party/BUILD` | §4.3 包装层 + ISA-L 集成 |
| 9 | `src/network/cpp/message_types.h:237` | `DataResponseMessage` 增 `payload_crc_`（字段列表尾部追加） |
| 10 | `src/storage/cpp/data_server.cpp:282-305` | 整帧快路径响应组装时 `data_checksum(raw)` 填入 |
| 11 | `src/network/cpp/data_client_pool.cpp`（raw 收满后） | 校验 wire 根：不匹配 → `ReadError::CHECKSUM`；帧头 check 位失败归同一类（连接作废） |
| 12 | `src/common/cpp/error_types.h:48` | `ReadError::CHECKSUM = 5`（注释：仅一次重取，此后 fatal） |

**分 chunk 校验 + trailer 格式（4 处）**

| # | 文件:位置 | 改动 |
|---|----------|------|
| 13 | `src/storage/cpp/compressing_streambuf.cpp:56-105`（`flush_chunk`） | 两分支写 `[i32][i32][u64 crc]`；完成侧追加 trailer（total_size/chunk_count/py_name） |
| 14 | `src/storage/cpp/decompressing_streambuf.cpp`（构造 + `refill`） | 构造改**尾部解析** trailer；块头 8B→16B，先验 CRC 再解压；`checksum_failed_` 标记 + `bool checksum_failed() const` |
| 15 | 消费出口翻译（5 处）：`decompress_helper.cpp`、`fly_stream.cpp` 读路径、`storage_export.cpp` `_read_decompressed`/`_read_from_db`、`data_server.cpp` py_name 解析、`database.cpp` low-tier 命中重解析 | trailer 尾部解析 + 解压后查 `checksum_failed()` → `ReadError::CHECKSUM` 上抛（替换现状静默 EOF 截断） |
| 16 | `src/storage/BUILD` | storage 目标 deps 补 common（data_checksum 传递） |

**对象级统一重取 + FATAL（2 处）**

| # | 文件:位置 | 改动 |
|---|----------|------|
| 17 | `src/storage/cpp/data_service.cpp`（对象读路径，含 `try_tier2_read`） | 统一重取预算 = 1（§5 语义全量落地） |
| 18 | 传播链：`data_service.cpp` → `Database::read_object_compressed` → Python `_read_decompressed`/`_read_from_db` | fatal → `RuntimeError("[FATAL-DATA-CORRUPTION] ...")` → TaskFailed 通道 |

### 6.2 测试（TDD 先行）

1. **FrameHeaderLayout**：encode ≥9B；测试内独立重算期望 header（不复用生产函数）；type 在 offset 8
2. **DeclaredLengthBeyond4GNoWrap**：声明 `len=5GiB` → `get_total_size` 返回 5GiB；9B buffer → decode 不完整且不消费；`raw_len_from_total(5GiB,16)` 正确
3. **SingleBitFlipRejected**：长度域/校验域各翻 1 位 → 拒绝
4. **GarbageRejected**：全零/伪随机 9B → 拒绝
5. **DataChecksumContract**：整块 == update 任意切分（1B×N / 1MB×64 / 不等长）；确定性；空输入固定初值；不同输入不同摘要
6. **ClientDetectsBadCrc**：fake server 故意发错 `payload_crc_` → CHECKSUM（决定性注入，零生产测试钩子）
7. **ServerComputesCrc**：真 DataServer 往返校验通过 + `payload_crc_` 非零
8. **TrailerRoundtrip**：块流+trailer 落盘格式；尾部解析；chunk 走读恰耗 `size−trailer_size`；raw passthrough 小对象同格式
9. **CorruptChunkDetected**：对已存 record 翻转某块 1 字节 → 解压出口 CHECKSUM（非静默 EOF）
10. **LocalRetryRecoversFromBadCache**：缓存条目损坏 → 重读盘一次成功
11. **LocalDiskCorruptFatal**：盘上损坏 → 重读仍败 → fatal 含 FATAL 标签
12. **RemoteChunkCorruptRetryThenFatal**（cb 注入）：CHECKSUM→CHECKSUM = fatal；CHECKSUM→断连 = fatal；CHECKSUM→干净成功 = 成功

### 6.3 步骤

| 步 | 内容 | 验证门 |
|----|------|--------|
| 1 | §4.3 包装层 + ISA-L 集成 + 测试 5 | `./fly.sh test //src/network/...` 绿 |
| 2 | §4.1 帧头（测试 1-4 先红 → 实现 → T2/T3 同步） | 网络单测全绿 |
| 3 | §4.4 trailer+块 CRC（测试 8/9 先红 → 写入 → 读侧 → 五出口翻译） | 网络+存储单测全绿 |
| 4 | §4.2 wire 根（测试 6/7 先红 → 实现） | 网络单测全绿 |
| 5 | §5 统一重取/FATAL（测试 10-12 先红 → 实现 + 传播链） | 单测+编译全绿 |
| 6 | 全量 QA → 50 轮稳定性 → 文档同步（network/module.md、P0-3、CHANGELOG）→ commit + push | 全绿 |

## 7. L2 实施计划（分 chunk 发送）

### 7.1 改动点

| # | 文件:位置 | 改动 |
|---|----------|------|
| 19 | `src/network/cpp/message_types.h` | 新增消息类型 `DATA_CHUNK`（seq/帧CRC/bytes 走两段式 raw 段）、`CHUNK_RESEND`（seq）、`DATA_DIGEST`（root/chunk_count）；`DataResponseMessage` 增 `total_compressed_len_`/`chunked_` 两字段（作 META） |
| 20 | `src/storage/cpp/data_server.cpp` | DATA_REQUEST 分流：缓存命中或 ≤ 单片阈值 → 整帧快路径（L0 已含 payload_crc_）；磁盘大对象 → pread 尾部 trailer 填 META → **单个自含 SendTask**（chunk 循环 pread + writev + `DataChecksum` 边发边算根 + 尾帧 DIGEST）→ rearm 读；读循环路由 `CHUNK_RESEND`（单块重发，每 seq 上限一次，计数在连接状态） |
| 21 | `src/network/cpp/data_client_pool.cpp` | 大对象分支：读 META → CHUNK 帧循环（帧头 check + 帧 CRC 验证，坏块记录 seq）→ 流后 `CHUNK_RESEND` 补块（每块上限一次）→ 收满 total_compressed_len → DIGEST 根验证 → 重组整 FlyBuffer 返回（L2 阶段 client 仍整缓冲，流式消费是 L3）→ 帧头/根失败 → `ReadError::CHECKSUM` |
| 22 | `src/network/cpp/net_quality_monitor.cpp` | （可选）分片流的 RTT/带宽采样口径适配 |

### 7.2 测试

23. **ChunkedRoundtrip**：跨多块大 payload 分片收发一致（真 DataServer）
24. **BadChunkResendRecovers**：fake server 注错某帧 CRC → client resend → 恢复成功
25. **ResendStillBadFatal**：resend 后仍坏 → CHECKSUM → 对象级 FATAL（对接 §5）
26. **DigestMismatch**：根不匹配 → CHECKSUM
27. **SmallObjectFastPath**：小对象/缓存命中整帧回归

### 7.3 步骤

| 步 | 内容 | 验证门 |
|----|------|--------|
| 1 | 消息类型 + server 自含 SendTask 分片发送（测试 23） | 网络单测绿 |
| 2 | client 分片接收 + 重组 + 根验证（测试 26） | 网络单测绿 |
| 3 | CHUNK_RESEND 在线重传（测试 24/25） | 网络单测绿 |
| 4 | 全量 QA → 50 轮稳定性 → 文档（network/module.md 消息类型总表）→ commit + push | 全绿 |

### 7.4 现有功能影响与兼容（2026-08-29 代码验证）

- **TIER2 接口零变化**：`DataClientPool::request` 返回签名（含 FlyBufferPtr 整块）不变，仅内部改分片接收重组——`try_tier2_read` 的副本轮换/退避/deadline、`record_remote_access`/`maybe_suggest_backup` 全兼容。
- **merge_db 收益修正（2026-08-29 代码验证）**：merge 是**压缩字节级中转**（`execute_merge_object`：`read_raw_compressed` 拉压缩字节 → 只解析 ObjectHeader → `write_record_checked` 零解压直写 .dat+idx），**不涉及解压/反序列化**——L3 对其无收益。真实收益映射：L2 作用于**传输段**（server 端不整读 GB 级对象 + 帧完整性对纯数据搬移价值最高）；client 侧当前仍整块中转（recv 整 C → 整写）。
- **merge 流式中转扩展点（L1+L2 组合，落地后优先做）**：L2 接收块流直喂 L1 的 `DataWriter::append_chunk`——收一块写一块，中转端峰值从 C 降到块级（~4MB）；纯字节管道，无需解压。
- **backup 链路兼容**：backup = 远程读（L2 范围，接口不变）+ `do_backup_write` 本地写（整块 API，不动）。
- **NET_PROBE 共存**：probe 走独立连接（worker_agent.cpp:763），与分片流互不干扰。
- **oneshot rearm 时序**：自含 SendTask 发完才 rearm 读端；resend 请求在发送期间到达则暂存内核接收缓冲，rearm 后由读循环路由——TCP 保序，安全（实现时以测试 24 验证）。
- **小对象/缓存命中整帧快路径不变**：现有 QA 全量（对象 ≤10MB）天然回归老路径。

## 8. L3 实施计划（读侧流式 v2：接收线程 + 有界队列 + Unpickler）

### 8.1 架构（真并行，2026-08-29 用户质询后定稿）

```
[server] chunk 循环连续 writev（不停）
   │  TCP（流控=平滑降速器，非断流）
[client 接收线程]（纯 C++ 无 GIL）：recv 块帧 → 验帧 CRC → 坏块 resend → 压缩块入有界队列
   │  有界队列（stream_buffer_chunks，默认 16 块/64MB 压缩态）
[消费线程 = 任务线程]：出队 → 解压（块 CRC）→ 喂 FlyStream/Unpickler 增量反序列化
```

消费不及时：队列吸收差值 → 持续慢消费才 TCP 降速到消费速率（**代价为零**：消费是瓶颈时关键路径不变；每流独占连接互不影响）。网络是瓶颈（常态）：队列浅、发送方全速。内存恒定有界：队列上限 + socket 缓冲 + R_obj。

### 8.2 改动点

| # | 文件:位置 | 改动 |
|---|----------|------|
| 28 | `src/storage/cpp/decompressing_streambuf.h/.cpp` | 输入抽象改**拉取源** `ChunkSource`（`pull(char*,size_t)` + 总长预告）；`MemoryChunkSource`（现行为，本地读不变）；`NetworkChunkSource`（持 fd/slot，阻塞 recv，析构归还） |
| 29 | `src/network/cpp/data_client_pool.h/.cpp` | `request_streaming()` → StreamHandle：接收线程（入有界队列）+ 消费拉取接口；每流一个接收线程，受 pool slot 封顶 |
| 30 | `src/storage/cpp/fly_stream.h/.cpp` | 读模式接受 `ChunkSource`（网络流） |
| 31 | `src/storage/export/storage_export.cpp` | `_read_streaming` 真流式化：返回 FlyStream（读模式）而非整块 bytes |
| 32 | `src/storage/py/database.py` | `read_object` cache="none"/大对象分支 → `pickle.Unpickler(FlyStream).load()`；high-tier 分支不变 |
| 33 | `src/core/cpp/config.cpp` | `stream_buffer_chunks`（默认 16）+ 大对象流式阈值（`streaming_read_threshold`，默认如 64MB 压缩后） |

### 8.3 测试

34. **TrueParallelism**：慢消费者（Unpickler 侧人为延迟）+ server 全速 → 接收线程持续推进（队列深度增长观测）
35. **BoundedQueueBackpressure**：持续慢消费 → 队列封顶不再增长 + 数据一致（TCP 降速路径）
36. **ResourceReleaseOnException**：Unpickler 中途异常 → fd/slot 归还（池可用性断言）
37. **StreamingVsWholeConsistency**：同一对象流式读 == 整读（含 CRC 全过验）
38. **MemoryCeiling**：大对象流式读 RSS 增量 << 对象大小（断言常数级）

### 8.4 步骤

| 步 | 内容 | 验证门 |
|----|------|--------|
| 1 | ChunkSource 抽象 + MemoryChunkSource 迁移（本地读行为不变回归） | 存储+网络单测绿 |
| 2 | request_streaming + 接收线程/队列（测试 34/35） | 网络单测绿 |
| 3 | FlyStream/导出/Python Unpickler 消费（测试 37/38） | 单测+编译绿 |
| 4 | 全量 QA → 50 轮稳定性 → 文档 → commit + push | 全绿 |

### 8.5 现有功能影响与兼容（2026-08-29 代码验证）

- **FlyStream 构造新增 ChunkSource 版本**：现有 `FlyStream(FlyBufferPtr)` 读模式构造不动——`export_macros.h` 宏（全部 C++ 导出对象路径）、`database.h` 模板、`storage_export.cpp` 六处、`data_service.cpp` 四处等既有消费零改动（DecompressingStreamBuf 构造签名 (data,size) 不变，trailer 解析是内部实现）。
- **Python file-like 面已就绪**（已验证导出）：`read(n)`/`readline()`/`readinto(buffer)`（Py_buffer 零拷贝）/`readable`/`writable` + `total_uncompressed`/`chunk_count` 属性——`pickle.Unpickler(FlyStream)` 无需新增 Python 面。
- **read_object 分支边界**：cache="high"（Python ReadCache）分支不变；C++ 对象 `_read_from_db` 路径不变；仅 cache="low"/"none" 且超阈值的 pickle 对象走流式。
- **fd/slot 持有期安全**（池结构已验证）：fd 有 `in_use` 标记，60s idle TTL 只清 idle——流式持有不被误清；`release_fd(fd, healthy)` mutex 保护可跨线程调用（StreamHandle 析构 = 消费线程归还）。
- **GIL 交互**：接收线程纯 C++（无 GIL），消费线程（任务线程）持 GIL unpickle——真并行成立；队列 mutex 无 GIL 依赖。
- **监控计量适配**：`record_read` 的 nbytes 在流式路径读完才可得（#32 已含）；TIER2 流量统计在流完成点采样。
- **QA 覆盖缺口**：现有 QA 最大对象 10MB < 阈值——老路径天然全回归，流式新路径需新增 QA case（阈值可配，测试用小阈值触发）。

## 9. L1 实施计划（写路径流式：压缩流逐块直入 WBQ）

### 9.1 改动点

| # | 文件:位置 | 改动 |
|---|----------|------|
| 39 | `src/storage/cpp/data_writer.h/.cpp` | 增量写 API：`begin_record()`（返回起点 offset，段事务内）/ `append_chunk(bytes)`（纯追加）/ `finish_record(trailer, object_name, orig_size, chunk_count, write_hash)`（append trailer + 登记 IndexEntry） |
| 40 | `src/storage/cpp/compressing_streambuf.h/.cpp` | dest 换 per-chunk sink（`flush_chunk` 完成即回调 sink(chunk_view)）——sink = 逐块构造 WriteRequest 入 WBQ；high_watermark 背压天然节流序列化端（队列 ≤10 块 ≈ 40MB） |
| 41 | `src/storage/cpp/database.cpp` | 大对象写路径（≥ `streaming_write_threshold`）：frozen/DUPLICATE **预检前移**到开写前 → 块流 WBQ → 完成单元（trailer + IndexEntry + `register_write`（compressed_size 此时精确已知）+ on_complete_ 上报 remote_idx）；小对象保留现整块路径完全不变 |
| 42 | `src/storage/cpp/fly_stream.h/.cpp` + `database.py` | write_object 大对象分支 `finish()` 不再返回整 buf（返回完成句柄） |
| 43 | `src/core/cpp/config.cpp` | `streaming_write_threshold`（默认如 64MB 原始） |

### 9.2 测试

44. **WriteMemoryCeiling**：大对象写 RSS 增量 << 对象大小（常数级断言）
45. **CrashOrphanChunks**：写一半中断 → 残块无 trailer 不可读 + 段事务回滚恢复
46. **StreamingWriteReadRoundtrip**：流式写 → 流式读（L3）一致，CRC 全过验
47. **WBQBackpressure**：high_watermark 生效（队列深度封顶 + 生产端阻塞观测）
48. **RegisterAtCompletion**：完成点注册语义——依赖任务在数据真实落盘后才可读（NOT_READY 窗口验证）

### 9.3 步骤

| 步 | 内容 | 验证门 |
|----|------|--------|
| 1 | DataWriter 增量 API（测试 45 的段事务部分） | 存储单测绿 |
| 2 | CompressingStreamBuf sink + WBQ 逐块（测试 44/47） | 存储单测绿 |
| 3 | commit 时序 + Python 路径（测试 46/48） | 单测+编译绿 |
| 4 | 全量 QA → 50 轮稳定性 → 文档（storage/module.md）→ commit + push | 全绿 |

### 9.4 现有功能影响与兼容（2026-08-29 代码验证）

- **适用边界（重要修正）**：L1 只优化「本地序列化写」（write_object/FlyStream 从 pickle 流开始）；**backup/merge 的写侧输入是整块压缩数据**（`do_backup_write` 直接 `record->take(move(compressed_data))`，database.cpp:440）——无序列化流可切，走现有整块 API 不变（其内存收益在读侧 L3）。整块 API 保留，增量 API 是新增不是替代。
- **task 事务段兼容**（已验证）：段由 `mark_write_begin/mark_write_end`（Database 层，task 首尾）控制——chunk 单元天然落在活跃段内；`abort_task_writes` 三步清理（clear_pending → drain → truncate）对块级队列兼容——chunk 单元不设 on_complete_ 副作用（只有 finish 单元设），被 clear 丢弃无残留。
- **register_write 时序**（已验证）：`register_write_with_master` 携带 size_bytes 上报（worker_agent.cpp:1730）；完成点注册时 size 精确；record_func 同步后移仍在 write_object 返回前（task 结算前）——时序安全；A 类断连重放语义更简单（写窗口断连时尚无 pending register）。
- **temp 路径不纳入**：`write_temp_pickle` 独立路径（compress_buffered_data → temp store），L1 不动（次级优化，后续按需）。
- **cache 语义按尺寸分档**：大对象（≥ streaming_write_threshold）populate_cache="low" 跳过缓存填充——读走磁盘 + NOT_READY 窗口（现有语义），需在 userdoc 注明分档口径。
- **QA 覆盖缺口**：同 L3——现有 QA 对象 ≤10MB 全走老路径天然回归；流式写需新增大对象 QA case。

### 9.5 尝试性增强：pickle 协议 5 消除 numpy 数组逐数组拷贝（尝试实现，不强制完成）

**背景**：主写路径已流式（`pickle.dump(obj, FlyStream)` 边序列化边写出），但 pickle 对大 numpy 数组在序列化时可能产生**逐数组的瞬时 bytes 拷贝**（in-band 序列化）。PEP 574（协议 5）的潜在收益：in-band 只读 buffer 直写（免拷贝）或 `buffer_callback` 旁路（真零拷贝，需读侧 `buffers=` 配合）。

**尝试内容**（用户裁定 2026-08-29：尝试实现但不强制要求完成）：
1. `database.py` 主写路径显式 pin `protocol=pickle.HIGHEST_PROTOCOL`（两端同版本二进制，读写兼容性无风险；现状依赖 DEFAULT_PROTOCOL 的隐式值）
2. 实测验证：大 ndarray `dump` 进 FlyStream 的峰值内存（有/无协议 pin 对比）——确认协议 5 的 in-band 路径是否已免逐数组拷贝
3. 若实测仍有拷贝且构成瓶颈：评估 `buffer_callback` 旁路方案（buffer 与块流分离传输，读侧 Unpickler(buffers=) 重组）——**复杂度超预期或验证不通过即放弃，记录结论后关闭，不阻塞 L1 交付**

**验收口径**：完成 1+2 即算尝试完成（结论无论正负均记录进本文档）；3 是可选深水区。**此项不阻塞 L1 合入与后续层排期**。

**尝试结论（2026-08-29 实测，Python 3.12：DEFAULT=4/HIGHEST=5）**：
1. ⚠️ pin `protocol=5` **尝试后回退**：numpy 数组在协议 5 下走 PickleBuffer 路径，与 FlyStream.write 的 bytes 参数不兼容（QA golden solver 4 例失败实证）；协议保持 DEFAULT。
2. ✅ 实测（400MB float64 ndarray）：协议 4/5 的 in-band dump 输出尺寸差 24B（frame 头），**峰值内存特征一致**（预热后两轮 maxrss delta 均为 0）——L1 流式写 + `pickle.dump(obj, stream)` 已把序列化输出流式化，**in-band 路径不存在逐数组拷贝瓶颈**——pin 无收益，回退零损失。
3. ⏹ OOB（buffer_callback）深水区按"验证不通过即放弃"关闭：当前主路径无收益场景（PEP 574 的收益在 out-of-band 传输——需要读侧 buffers= 配合的架构改动，而实测 in-band 已无瓶颈）。**尝试完成（结论为负），不阻塞任何层**。

## 10. 内存量化（GB 级对象，C≈0.5R，4 并发）

| 阶段 | 现状 | L0-L3 后 | +L1 后 |
|-----|------|---------|--------|
| server | 4×C ≈ 2GB（整读 + send pin） | ~16MB（4 传输 × 4MB 片缓冲） | 同左 |
| client（每拉取） | C + 2R ≈ 3GB | ≈ R（Python 活对象）+ 队列 64MB | 同左 |
| 写（本机） | R + 2C | 同现状（L1 未做时） | R + ~48MB 常数 |

## 11. 风险与对策

| 风险 | 对策 |
|------|------|
| 协议原子切换（混合版本） | 同一代码库同一二进制，early-dev 无混部（F7 前提）；QA 全量覆盖 |
| 旧格式 db | 用户裁定不考虑版本兼容：新读侧尾部解析 → 旧 record 必败 → 显式 CHECKSUM（非静默），重建 db；QA 每轮全量重建覆盖 |
| refill 原静默 EOF 行为变化 | 损坏从"静默截断"变"显式失败"是本变更目的；依赖截断降级的测试当场暴露，按根因改造 |
| 目标机缺 `libisal2` | 动态链接缺失 = 启动即失败（显式）；部署清单注明；集群机统一 apt |
| 未来更换校验方案 | 只改 `data_checksum.cpp`；接口与契约测试不变 |
| CRC 计算占用数据面线程 | 14.6 GB/s（64ms/GiB）；块 CRC 与压缩/解压同点计算；L2 根摘要边发边算单遍 |
| 多 send 线程乱序写同 fd | L2 分片发送封装为单个自含 SendTask（§7.1 #20） |
| 慢客户端钉住 send 线程 | L3 接收侧队列 + 内核缓冲把触发概率压到"仅持续慢消费"；届时上调 `data_server_threads`（配置已存在）；远期可改非阻塞 epoll 发送 |
| bad_alloc（巨型对象 vs 垃圾声明长度） | check 位拒绝垃圾帧头后，client `resize` 仍包 try/catch → NETWORK 错误 |
| FATAL 语义误伤 | 仅 CHECKSUM 类触发；QA 注错走 fake server / 内存翻转，不触真集群 |
| L1 写路径崩溃一致性 | trailer commit marker + 段事务 ABORT truncate 双保险（§4.4/§9 测试 45） |

## 12. 不做清单与触发条件

**不做**：credit 窗口流控（负载画像无此问题）；**断线续传**（整对象 TIER2 重试覆盖；在线块重传已纳入 L2）；块级定位/诊断簿记（用户裁定无意义）。
~~写入时块索引落盘~~（**2026-08-29 废止**：块位置表以 trailer 内嵌形式纳入 §4.6 统一块模型 B' 项）。

**L4 触发条件**：需要部分读（solver 子域切片）或块粒度缓存逐出时立项（≈亿级落地）。

## 13. 实现记录（2026-08-29 L0-L3+L1 全层落地）

### 13.1 实现中遇到的问题与修复

| # | 问题 | 根因与修复 |
|---|------|-----------|
| 1 | trailer 尾部解析最初按 [fixed][py_name][crc] 布局实现失败 | ObjectHeader::serialize 的 py_name 在 fixed **之后**——尾部无法定长锚定。修正为 trailer 专用布局 **[py_name][fixed][crc]**（serialize_trailer），尾部解析 O(1) |
| 2 | cc_shared_library 符号冲突（data_checksum 被多 so 静态链） | exports_filter 方向错误（会排除静态链导致 undefined symbol）；正解 = data_checksum 独立 so + 各 so dynamic_deps；-lisal 必须在 cc_library 自身 linkopts（so 不透传 deps 的 third_party linkopts） |
| 3 | MessageProtocol::decode 需要含 9B 前缀的完整帧 | client 流式读帧时只传 payload 会解析失败——DIGEST/resend 解析必须重组 9B+payload |
| 4 | client 分片重组偏移错位 | 切片尺寸未随协议携带——META 增 chunk_frame_bytes_（发送端实现细节必须显式告知） |
| 5 | Python 面 `from core import config` 不存在（QA 110 例失败） | core 导出的是 Config 类/get_config 函数。教训：Python 面改动至少做 import 烟测（QA 一轮代价） |
| 6 | nanobind 类方法/模块函数返回 unique_ptr 均转换失败 | nanobind 无 pybind11 的 holder 概念——裸指针 + `rv_policy::take_ownership` 是唯一可靠路径（WriteStreamHandle 嵌套类方案因此整体废弃，改为 FlyStream 直持 commit 回调） |
| 7 | 协议 5 pin 导致 QA 5 例失败 | numpy 在协议 5 下走 PickleBuffer/bytearray，与 FlyStream.write 的 bytes 参数不兼容；且实测协议 4/5 in-band 内存特征一致——按 §9.5"验证不通过即放弃"回退 |
| 8 | 流式写破坏 abort 段回滚（load_db_abort 失败） | mark_begin 时序在 begin_incremental **之后**（回滚点=写后位置）——前移到 begin 前（对齐 commit_write 的 execute 时序） |
| 9 | QA 运行中执行 build+install 导致整轮无效 | build/ 软链指向 bazel-bin，中途构建替换二进制——**QA/稳定性运行期间禁止构建**（操作纪律） |
| 10 | QA 期间反复触发 pread trailer 的 META 预解析失败窗口 | 无此问题（防御性记录）：server 尾部 pread 失败时 py_name 空 + trailer_len 0——L2 重组路径不受影响，L3 消费端回退整缓冲（保守正确） |

### 13.2 风险项（如实记录）

| 风险 | 现状与对策 |
|------|-----------|
| TIER2 流式仅首副本 best-effort | 失败即回退 read_object_compressed 完整编排（副本轮换/退避/零容忍语义无损失）；流式中断续传的 TIER2 编排留待 L4+ |
| L1 盘写在任务线程（同步） | write 进 page cache 即返回（与 WBQ 后台 execute 延迟特征一致）；真后台逐块 WBQ 留作后续（需处理块顺序与完成单元时序） |
| WorkerGracefulExitClassifiedAsExited 偶发（全仓并行 2 次/数十次；单测 26 次 + 全仓 3 次不复现） | WORKER_EXIT 发送路径未被本次改动触碰、无因果链；100 轮稳定性期间若复现即抓现场（gdb attach / 日志取证） |
| 增量写 entry 的 write_context_hash 留空 | master 侧 register 的 hash 是权威（读侧 provenance 校验用 register 值）；与 write_record 路径的 entry-hash 双轨现状一致 |
| CHUNK 帧的 seq 越界检查依赖 chunk_count 推导 | META 的 frame/total 驱动——已含协议失步防御（越界即断） |

### 13.3 设计决策记录（不确定是否符合 fly 哲学的如实说明）

1. **CHUNK 片 = 纯字节切片**（非磁盘块边界）：换取"client 重组后与磁盘 record 字节一致、DecompressingStreamBuf 零适配"。代价：重传单位与磁盘块解耦（重传 4MB 而非单个坏块）——坏片极罕见（内存/网络缺陷），代价可接受。
2. **TIER2 流式 = 首副本 + 回退**（而非完整流式副本轮换编排）：避免把 try_tier2_read 的轮换/退避/预算逻辑重写成流式版（重写=新缺陷面）。判断依据：fly 哲学"根因优先、不引入额外复杂度"——回退路径是已验证的完整语义。
3. **streaming_read/write_threshold 语义 = 功能开关**（非尺寸阈值）：读/写前不知对象大小，探 size 需额外 IO 得不偿失。若按字面实现尺寸分档反而引入"先 dumps 估算"的内存反噬。
4. **DUPLICATE 预检未前移**（偏离计划 #41 原文）：register 时序安全在 §9.4 已验证（commit 8419526 家族修复锁定）；前移需改注册协议时序（高风险区），大对象 DUPLICATE 场景罕见。
5. **backup/merge 写侧不走流式**（对齐 §9.4 适用边界）：其输入是整块压缩数据，无序列化流可切——L1 只优化本地序列化写主路径。

### 13.4 验证状态

- 单元测试：73/73（新增 data_checksum 契约 5 例 / 帧头 4 例 / record 格式 4 例 / wire 根 2 例 / 零容忍 3 例 / 分片 5 例 / 流式源 4 例）
- 全量 QA：165/165（L0 后、L2 后、L1+L3 后各一轮全绿）
- 100 轮稳定性：进行中（结果见 stability_test 产物目录）

