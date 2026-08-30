# V2 chunked-transfer 性能分析报告（2026-08-30）

> 触发：用户观察 100 轮稳定性测试中每轮 QA 运行时长较 V1 增长约 10s，要求以性能
> 分析工具定位瓶颈、增强易解项、记录其余瓶颈。
> 方法：stability 产物 per-case 耗时对比（40 轮均值）→ perf 采样（dwarf 调用链）
→ 数据画像实测。数据存档 `.work/perf/`（perf.data + script 文本）。

## 1. 回归量化

| 口径 | V1（8/29 基线） | V2（8/30） | 回归 |
|------|----------------|-----------|------|
| 每轮墙钟中位数（runqa -j6 全量） | ~1m36s | ~1m45s | **~+10s（+10%）** |
| 全量 per-case 耗时加总（单线程口径） | 531.8s | 582.3s | +50.5s |
| 代表 case：test_temp_zero_copy | 11.7s | 14.3s | +2.6s |
| 代表 case：test_solver_ras_matrix（13 组合） | 28.3s | 32.8s | +4.5s |

回归集中在两类 case：solver/golden 类（数据传输密集）与存储路径类
（test_temp_zero_copy / test_locality_perf）。启动/网络/消息类 case 无回归。

## 2. 根因分解（按贡献排序）

### 2.1 主因：§4.7 low-tier cache 取消的重复读（设计裁定代价，非缺陷）

用户裁定（2026-08-30，§4.7）：远程读统一走数据源，只有"无缓存"与"high level
缓存"两种。直接后果：

- **V1**：master 重复读远程对象时，2nd/3rd 次命中 low-tier cache（压缩数据
  shared_ptr，零网络零磁盘）；
- **V2**：每次读完整走"远程流式（帧+块校验）+ 解压 + unpickle"。

test_temp_zero_copy 数据画像（实测）：10MB `list(range())` → pickle 52.3MB →
LZ4 后 ~30MB/对象。3 轮读 × 5 对象在 V2 下是 **15 次全量远程流式**；其中
unpickle（52MB 反序列化 + 前一轮 list 释放）占 case CPU ~44%（perf children：
_pickle.load 44.3% + list_dealloc 23.8%），是 case 固有最大开销。

**结论**：该部分回归是 §4.7 裁定的直接代价，预期内。重复读场景的加速应由
high-level cache（Python 对象缓存，显式配置）承担——数据消费方按需配置。

### 2.2 次因：传输路径 CRC 计算量 ×5（V2 校验语义的 CPU 代价）

V2 对同一数据引入多层校验，CRC 总量对比如下（以 test_temp_zero_copy 为例）：

| 层 | V1 | V2 |
|----|----|----|
| 写路径块 CRC（flush_chunk） | 无 | 5×30MB = 150MB |
| serve 端帧 CRC（on_readable） | 150MB×1 | 450MB（3 轮全传） |
| 接收端帧 CRC（DataClientPool） | 150MB×1 | 450MB |
| 接收端块 CRC（DecompressingStreamBuf） | 无 | 450MB |
| **合计** | **~300MB** | **~1500MB（5 倍）** |

perf 实测：crc64（ISA-L crc64_ecma_refl_by8，标称 14.6GB/s）占 case 采样
2.81%（~290ms 单跑）。调用者分布：serve 帧 CRC 41.8%、接收帧 CRC 37.8%、
解压出口块 CRC 15.3%、写块 CRC 5.1%。CRC 与 unpickle/LZ4 同为内存带宽密集，
runqa -j6 并行下相互竞争放大（单跑 ~200ms 级增量在并行口径放大至秒级）。

注意：CRC 算法本身已最优（ISA-L PCLMUL），帧大小 4MB（帧头 16B 占比 4e-6），
**开销来自"校验几遍"而非"单遍多慢"**。

### 2.3 双拉缺陷（pread 实验发现，同日修复）

read_object 的 Python 层先调 `_get_py_name` 探测对象是否 C++ 类型——C++ 侧
为此**完整执行 read_object_compressed（全量拉取）只取几字节 py_name，数据
丢弃**；随后实际读取再拉一遍。实测 64MB 落盘对象：双拉 638 vs 单拉 1052
MB/s（**传输量翻倍**）。§2.1 表列的"3 轮全量远程流式"实际是 6 次传输——
重复读回归的最大成分是双拉放大。

修复（2026-08-30）：py_name 由读取原语天然携带（流式 stream.py_name /
整缓冲 _read_decompressed 返回值），Python 层反转分流——pickle 路径单次
拉取；C++ 对象路径交 _read_from_db（ObjectCache 命中零拉取，miss 重建一次，
总量与旧探测路径持平）。实测热读 1072 / 冷读 494 MB/s（均与单拉参照持平）。


### 2.4 微量项（均已核实为非瓶颈）

- 帧头 16B（V1 8B）：帧 4MB 下可忽略；
- trailer 块表序列化/对账：O(块数)（30MB/256KB ≈ 120 块），μs 级；
- DiskChunkSource pread 拉取式本地读：PageCache 命中，μs 级；
- 预许可双 RPC：仅流式写路径每对象一次额外往返，ms 级；
- WBQ 逐块值捕获（CMString 拷贝）：一次 memcpy/块，ms 级。

## 3. 已实施优化（本轮）

1. **`Database::read_object_compressed` 死代码删除**（src/storage/cpp/database.cpp）：
   §4.7 取消 low-tier put 后，第二段 `deserialize_trailer`（按 trailer total_size
   折算 accounted 记账尺寸）无任何消费者，每次读重复解析 trailer 纯浪费——整段
   移除。
2. **test_temp_zero_copy docstring/验证指南修正**：原文本仍指导验证已取消的
   "2nd/3rd read: ObjectCache.low hit" 行为，更新为 §4.7 语义（重复读完整远程
   流式，加速由 high-level cache 承担）。

验证：storage 单测 22/22、全量 QA 167/167 通过。

## 4. 帧 CRC 冗余消除（已实施，2026-08-30 用户裁定"先消除CRC冗余"）

**原状**：同一字节的完整性由三层校验覆盖——发送端帧 CRC（写入帧头）→ 接收端
帧 CRC（帧到达即验）→ 接收端块 CRC（块收齐后验，解压前权威校验）。写路径另有
一遍块 CRC。传输路径 CRC 总量 = 3 遍。

**实施**（§14.8 详录）：发送端两处帧片 CRC 取消计算（字段恒 0 = 未计算）；
接收端两处 `crc != 0` 才验（0 跳过 / 非 0 旧协议端兼容仍验 + resend 定位）。
完整性收敛：块级 CRC（流式路径 resend 驱动不变）+ DIGEST 根摘要（Span 路径
收满权威校验）+ trailer CRC。

**实测收益**：QA 全量每轮 107s → **96.1/96.8s（复跑确认）**——§1 的 +10s
回归全部回收。crc64 采样 98→82（-16%），剩余构成：DIGEST 双侧 66%（serve
root.update + client 收满整算）+ 解压出口块 CRC 33%。

**语义代价（已接受）**：
1. 损坏检测时机从"帧到达即验"推迟到"块收齐（流式）/收满（Span）"——反馈
   延迟毫秒级，localhost/内网无感；
2. Span 路径损坏定位退化为对象级重取（digest CHECKSUM → TIER2 零容忍预算
   重取），帧级精准 resend 仅保留于旧协议端兼容路径；流式路径 resend 由块
   CRC 驱动、粒度块区间（256KB 级），反而更小；
3. 块边界的帧——块 CRC 独立覆盖各自块，无覆盖空隙，安全性不降。

**第二轮候选（未实施，同属冗余）**：DIGEST wire 根摘要双侧计算占剩余 crc64
的 66%——在块级 CRC + trailer CRC 双层存在下同为传输层冗余校验（协议帧
DIGEST 保留作流终止信号，仅 root_crc 填 0 + client 跳过）。预计 QA 每轮再省
2-4s。涉及 §4.5 wire 根摘要条款（乱序/调包兜底语义弱化——覆盖校验仍在），
待裁定。

这与 §5"零容忍语义"不冲突（块 CRC 仍是端到端权威校验，FATAL 通道不变），
但**帧 CRC 属 §4 定稿协议的一部分**，检测层次的变化需用户裁定后实施。

## 5. 其余瓶颈记录（不本轮处理）

| 瓶颈 | 量级（test_temp_zero_copy 单跑） | 说明 |
|------|--------------------------------|------|
| unpickle（Python 反序列化） | ~44% CPU | 固有：52MB pickle 的 int 对象构造与释放。改善方向是应用层避免巨型 list（numpy 数组走 PickleBuffer 零拷贝通道，但受 pin 协议限制——见 §13 记录）；属使用模式问题 |
| 序列化端（worker 写任务） | ~23% CPU | pickle.dumps + LZ4，固有 |
| 动态链接/TLS（_dl_update_slotinfo 等） | ~15% CPU | nanobind 多 .so + 多线程 TLS 访问的环境开销；预链接（prelink）/static pie 可削减，收益有限风险高，不动 |
| 内核页服务（clear_page/page_fault） | ~10% CPU | 大缓冲分配的缺页开销，与 52MB 数据画像匹配；已由 FlyBuffer 池化部分缓解 |
| OpenBLAS 线程池自旋 | solver case 采样 ~60%（噪声） | scipy OpenBLAS spin-wait；对 QA 耗时有干扰但非 fly 代码；可设 OPENBLAS_NUM_THREADS=1 于计算量小的 case，属测试环境调优 |

## 6. 结论

- V2 每轮 QA +10s 回归 = **§4.7 缓存裁定的重复读代价（主）+ 校验层数增加的
  CPU 开销（次，~3%）+ 并行内存带宽竞争放大**；
- 无实现级缺陷（无异常拷贝、无重复解析残留——死代码已清）；
- 性能敏感场景（重复读大对象）建议显式启用 high-level cache；
- **帧 CRC 冗余已消除（2026-08-30 裁定实施，§4）**：QA 全量每轮 107s →
  96.1/96.8s，回归全部回收；下一轮候选 = DIGEST 根摘要双侧消除（占剩余
  crc64 66%，预计再省 2-4s，§4 第二轮候选，待裁定）。
