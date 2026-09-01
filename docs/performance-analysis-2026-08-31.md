# 框架运行时性能热点分析（2026-08-31）

> 本文章节号保留历史跳号（§4 之后直接 §6，原 §5 并入 §4），非缺漏。

> 触发：用户要求确认框架自身（传输/存储/调度运行时，非 QA 框架开销）还剩
> 哪些可提升点。方法：micro-bench 复测对照历史基线 + 自定义放大压测
> （排除 Python 数据画像干扰）+ perf dwarf 调用链归因。

## 1. 方法

QA case 的 perf 采样被 Python 大对象画像淹没（52MB list 的 unpickle/GC 占
~60%，见 2026-08-30 报告 §2.1），框架 C++ 侧无线索。本轮构造纯传输压测：
256MB **不可压缩随机数据** × 1 写 + 30 次跨 worker 远程读
（`read_object(cache="none")` 强制全程 wire）= 7936MB wire 流量 / 6.5s，
goodput **1218 MB/s**（优化前基线）。此时传输路径占绝对主导。

读侧每字节拷贝链（优化前）：recv（内核）→ 块提取 append → 队列 emplace
→ pull 拷贝 → 解压 → unpickle，另有三处全量 memset 浪费。

## 2. 已实施优化（b8d5ebf + 2881b3c）

### 2.1 三处全量 memset 纯浪费（perf self 9.08% → 0.38%）

| 位置 | 浪费 | 修复 |
|------|------|------|
| DecompressingStreamBuf::refill | 压缩块 `CMVector<char> comp_buf(comp_size)` 构造清零 256KB/块 → pull 立即覆盖 | overwrite 分配 `new char[]` |
| DecompressingStreamBuf::refill | `buffer_.resize(uncomp_size)` 每块清零 → 解压立即覆盖 | 仅首块扩容触发；无压缩分支直接拉入 buffer_（免 swap 中转） |
| NetworkChunkSource::read_one_frame | 每帧新建 FlyBuffer + resize 清零 4MB → recv 立即覆盖 | 跨帧复用（流内帧长恒定，resize 第二帧起 no-op） |

量化：256MB 对象读一次 = 768MB 无效 memset；30 次读 = 23GB memset。

### 2.2 feed_frame 块起点原地解析

帧 4MB 与块网格不对齐（随机数据下块全长 16+comp 非 2 的幂），跨帧块必然
暂存；但块起点且整块在帧内时（正常数据多数块）从帧指针直接验 CRC + 交付
（consume_block，快慢路径共用），免 parse_buf_ 全量 append。

### 2.3 Logger localtime_r（b8d5ebf）

`logger.cpp:168` 原 `std::localtime`：TZ 未设置环境（本机如此）下 glibc
每次调用 `tzset_internal(always=1)` → stat(/etc/localtime) + 重读解析
（strace 实证：1000 次 localtime = 1000 次 stat，对照 0）。每条日志一次
多余系统调用 + 非线程安全接口。改 `localtime_r`（always=0 零系统调用）。

### 2.4 效果

| 指标 | 优化前 | 优化后 |
|------|--------|--------|
| goodput（7936MB wire） | 1218 MB/s | **1322 MB/s（+8.5%，3 次稳定）** |
| memset self 采样 | 9.08% | 0.38% |
| 全量 QA | 85.6s | 83.9s（波动内） |

验证：storage 21/21、log 单测全绿；全量 QA 167/167。

### 2.5 wire 根摘要残留消除（root_.update，2026-08-31 用户裁定）

T5 消除了 serve 端根计算与 client 端最终验证，但 `deliver_bytes`/
`drain_pending` 的增量 `root_.update`（ISA-L CRC64 全量遍历）无条件残留——
读路径第三遍 CRC（~8% 传输 CPU）。原保留理由是旧 serve 兼容（root_crc≠0
时仍可验根）；裁定不存在版本差异、不考虑兼容后整段删除：`DataChecksum
root_`/`root_expected_`/`digest_chunks_` 成员及根失配检查移除，DIGEST 帧
仅保留作流终止信号。接收线程每字节减一次 CRC64 遍历。

### 2.6 serve 端 chunk 发送零拷贝（f38488f）

CHUNK 循环从 pread 4MB + writev(头+payload) 改为帧头 29B 单独 send +
payload `::sendfile`（file→socket 内核直通）：
- 消除 serve 端 pread 的 file→用户 buffer 拷贝（`copy_page_to_iter`/
  `filemap_read` ~9%）；
- serve 端不再持有 4MB 单片缓冲（每连接省 4MB 内存与构造清零/缺页）；
- server 本就不碰数据字节（帧片 CRC 发 0），零拷贝不改变完整性语义。

实现：`Transport` 接口新增 `send_file`；`TCPSocketTransport` 非阻塞
sendfile 循环（EAGAIN→poll POLLOUT）；resend 重传路径保持 pread+sendv
（低频，改动面最小化）。

**效果：7936MB 压测 goodput 1322 → 1586 MB/s（+20%，3 次稳定）**。

## 3. 确认为语义成本、不再追的项

| 项 | 量级 | 定性 |
|----|------|------|
| ~~serve 端 pread 文件读拷贝~~ | ~~6-9%~~ | **已消除（f38488f，§2.6）：sendfile 零拷贝，goodput +20%** |
| 内核 socket 拷贝（copy_user） | ~27-30% | loopback TCP 收发必然；消除需共享内存 transport，属大改另议 |
| 块 CRC（写 1 + 验 1） | ~18% | 零容忍语义最低成本（T5 消 DIGEST 后帧级已零），ISA-L 已最优 |
| unpickle bytes 拷贝 | — | pickle 语义必然 |
| WriterPrefRwLock 写优先下读写混合读吞吐降 | bench 口径 7-8x | 防写饿死裁定语义（perf-baselines.md 已补注新口径）；真实负载写频率低 |

## 4. micro-bench 现状（对照 perf-baselines.md）

- DataService C/D 场景、调度 A/B 场景：与历史基线持平；
- DataService B 场景：历史"优化后"数字失效（锁语义变更，见 §3 与
  perf-baselines.md 口径补注）；
- 调度 C 场景 73.5k → 33k tasks/sec：bench 未变，8/4 后调度器功能演进
  （locality/handler-lane）+ 每任务日志开销；真实调度频率远低于此，无感。

## 6. 读写路径并行度确认与三段拆解评估（2026-08-31 增量）

### 6.1 当前并行度（代码 + 实测确认）

| 路径 | 分段 | 线程模型 | 状态 |
|------|------|----------|------|
| 读 | net（接收）/ dec（解压+块CRC）/ unp（反序列化） | net 独立接收线程；dec+unp 同在消费线程交替 | 两段流水线 ✓ |
| 写 | ser（序列化）/ comp（压缩+CRC）/ io（盘写） | ser+comp 同在任务线程交替；io 独立 WBQ 线程 | 两段流水线 ✓（chunk 级：WBQ 有块在写起，ser+comp 与 io 并行） |

### 6.2 两段实测比例（1GB urandom，perf 线程分解）

**写**：ser（pickle 150ms）+ comp（LZ4 store ~350ms）+ crc（70ms）≈ **0.6s**
（主线程，perf 线程样本数自洽）；io 段独立口径（同盘纯写 1GB 文件）实测
3552-3671ms（fsync 口径 7.5s，盘抖动时）。**db 全链路写 2157-3552ms ≈ 纯 io
口径**——ser+comp 完全重叠在盘写窗口内，端到端 ≈ max(0.6, io) ✓。如实说明：
下界 2157ms 比对照项（io 3552-3671ms）小约 40%——纯写对照含 fsync + WBQ
后台 flush，高估 db 写 io 段，故本对照属「io 下界证据」，非严格 max 验证
（「ser+comp 完全重叠于盘写窗口」的判断不受影响）。两轮测量
均未出现「端到端 ≈ ser+comp+io」的串行特征；首轮分析曾把 io 段误写为
2.4s（端到端倒推值，循环论证，84c7806 已废弃），以本节独立口径为准。

**读**：net ≈ 750ms（接收 1.4GB/s）；dec+unp ≈ 570ms（解压+块CRC ~340ms +
unpickle 156ms——本地基准实测）。**比例 ≈ 1.3:1**。端到端 828-900ms ≈
max(750, 570) + 首尾块延迟 ✓；TX 时间线证明传输摊满全程（消费反压把 serve
拉平到消费速率）。串行和口径 1316ms 被并行压缩到 ~860ms。

### 6.3 三段拆解边际收益评估 → 不拆

三段全拆的理论收益 = max(两段分组) − max(三段独立)，仅当「压缩段成为最大段」
时非零。实测段耗时排序：

- 写：io(3.6s±盘抖动，§6.2 独立口径；原 2.4s 为废弃倒推值) > comp(0.35s) >
  ser(0.15s)——comp 恒小于 io 与 ser 之和，
  拆出后 max 不变；
- 读：net(750ms) > dec(340ms) > unp(156ms)——拆出 dec 后 max 仍 = net；
- 压缩段是 C++（LZ4 ~2-4GB/s），结构性快于 Python 序列化（GIL）与盘/网 IO，
  **在 LZ4 + localhost/内网体系下不可能成为瓶颈段**。

结论：**不拆三段**。当前两段流水线已实现 max(段) 行为（读侧 1316→860ms、
写侧 ser+comp 完全重叠即证据），拆解只增加线程/队列/保序/flush 语义复杂度
而无端到端收益。**触发重评的条件**：换高速 IO（NVMe 7GB+ / 共享内存
transport）使 net/io 不再主导，或换重压缩算法（zstd 高级别）使 comp 浮出——
届时拆点现成（写：`CompressingStreamBuf::flush_chunk` 边界；读：
`NetworkChunkSource` 队列尾与 `DecompressingStreamBuf` 之间）。

## 7. RPC 数据交换路径优化（2026-08-31 增量）

针对 solver 动态链（ras_graph_dynamic）的 worker 间 PeerRpc 数据交换：

### 7.1 PeerRpc 专用直拼帧（拷贝消除）

大 payload 一次往返原链路每字节 **6 次**用户态拷贝：发送端
`msg.payload_` 赋值 + bitsery 临时缓冲 + frame 复制；接收端 substr +
`FLY_DECODE` 内部 `take(CMString(input))` 再拷 + bitsery 输出。改为
PeerRpc 专用直拼帧（两端同仓库同步，内部闭合；帧外格式 9B 头不变）：

```
REQUEST:  [8B frame header][1B type][8B rpc_id BE][8B src BE][payload]
RESPONSE: [8B frame header][1B type][8B rpc_id BE][1B status][payload]
```

发送端单 buffer 组装、payload 仅 memcpy 一次；接收端字段直读、payload
单次构造——payload 中间拷贝 6 → 1（剩余为 Python 边界与内核，必然）。
MessageHeader（message_id/timestamp）在 PeerRpc 链路无消费者，随裁定
（无版本差异、不考虑兼容）移除。

### 7.2 圈级收集并发化（sum → max）

动态链 check 侧收集圈原为逐成员串行阻塞（迭代延迟 = Σ成员往返）。改为
线程池并发发起（`peer_rpc_call` export 层已释放 GIL，等待真并行；各成员
独立连接，C++ PendingRpcMap 按 rpc_id 天然支持并发 pending）——迭代延迟
Σ成员 → max(成员)。

**本机实测无差异**（703 vs 704ms/25 迭代）：每迭代收集等待仅
1-3ms，大头是任务链调度（~190ms/步）与 check 单线程编排。收益在跨机
部署（RTT × 成员数）时显性，语义等价（NOT_READY 重试/超时判死/断连
唤醒全保留），保留。

### 7.3 RPC 数据面语义要点（实测口径）

- 编解码 CPU 占比 <0.5%（solver case perf 采样）——拷贝消除的价值在
  大 payload（MB+）场景的线性放大，本机 QA 口径微秒级；
- 收集等待 <3ms/迭代；任务链调度与 Python 编排才是迭代延迟大头
  （§6.2 同口径）。

## 8. 结论

传输接收路径的"纯浪费"（无效 memset + 中转拷贝）已消除，goodput +8.5%；
剩余 CPU 构成 = 内核拷贝（TCP 必然）+ 块 CRC（语义必需）+ 队列/解压/
unpickle（设计固有）。框架 C++ 自身已无实现级缺陷型热点；下一个数量级
提升需换传输机制（共享内存 / splice），属架构级选项另行裁定。

---

> **2026-09-02 注**：§2.1/§6.3 引用的 `DecompressingStreamBuf::refill`/
> `flush_chunk`/`buffer_` 等符号已随 Stage 管线迁移（fd2bdcb
> ——CompressingStreamBuf/DecompressingStreamBuf 换 fly::WritePipeline/
> fly::ReadPipeline 实现：现 ReadPipeline::scratch_ 跨块复用、
> WritePipeline::flush_block 出块），类名仍作薄壳保留；本文历史分析
> 口径与结论不变。
