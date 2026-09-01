# PeerRpc 流式大 payload 管线（流插件化）设计记录

> 2026-09-01。目标：worker 间业务 RPC 的大 payload 传输复用 read/write_object
> 的压缩块流管线（用户设计：write 侧 WBQ 抽象为 sink、read 侧数据提供抽象为
> source，RPC 接入同一对抽象）。无旧版本兼容包袱（项目准则）。

## 1. 协议（PeerRpc 连接复用，连接独占）

```
[PEER_STREAM_START]  rpc_id + direction(0=请求/1=响应) + compression_type
[DATA_CHUNK ×N]      复用数据面 4MB 切帧格式（帧 payload = 块流字节，块头自描述）
[PEER_STREAM_END]    rpc_id + total_uncompressed + chunk_count + consumed 对账
```
- START 前置 comp_type（序列化开始即知）；END 后置对账（total/块数是流尾
  事实）——trailer 的块表/固定域在顺序流场景无消费者，不进 RPC 管线。
- 完整性：块级 CRC（写入时刻锚点，逐块自校验）+ END 三重对账。对账失配 /
  坏帧 = 零容忍 close 连接（调用方由 DISCONNECT 唤醒 fail）。
- MessageHeader（message_id/timestamp）在 PeerRpc 链路无消费者，随"无版本
  差异、不考虑兼容"裁定移除。

## 2. 接口

```python
w = agent.peer_stream_writer(conn_id, compression="lz4", level=-1)  # 请求流
pickle.dump(obj, w)          # file-like：序列化直入压缩管线，无整体缓冲
w.finish()                   # 尾块 + END 对账
status, resp = agent.peer_stream_call_wait(w.rpc_id(), 0)   # 等响应

w = agent.peer_stream_respond_writer(conn_id, rpc_id)       # 响应流
pickle.dump(resp_obj, w); w.finish()
```
- 压缩算法/级别接口级指定（config 仅默认值）——业务可预估数据规模选择
  激进压缩；块级 85% 规则自动止损（压缩率不达标的块 raw 直通，
  comp==unc 隐式标记，零额外拷贝）。
- 读端变体（payload 包成 PeerStreamBuffer，pickle.load 直用——readinto
  直填 pickle 工作缓冲，免中间 Python bytes 全量拷贝；status 非 OK 时
  buffer 承载 reason 文本，to_bytes 可读不丢诊断）：
  `req = agent.peer_rpc_recv_request_stream(0)`（超时返回 None）→
  `(conn_id, rpc_id, src, buf)`；`status, buf = agent.peer_stream_call_wait_buf(rid, 0)`。
- 状态码：PeerRpcStatus::OK = 1（内部枚举，PENDING=0）——调用方判 OK 用 1。

## 3. 基准（自环 1 worker，256KB-512MB，3 轮中位，随机数据）

| payload | single-frame 基线 | stream-lz4 | stream-zstd9 | stream-none |
|---------|-------------------|-----------|--------------|-------------|
| ~0B     | 164 MB/s | 128 | 108 | 192 |
| 4MB     | 311 | 223 | 127 | 227 |
| 16MB    | 337 | 290 | 144 | 285 |
| 64MB    | —（基线限 ≤16MB） | 224 | 136 | 208 |
| 512MB   | —（单帧大 payload 触发内存峰值/失败） | 38.3 | — | 34.2 |

结论：≤16MB 单帧略优（单帧路径轻）；流式在 64MB+ 稳定可用且无内存峰值
爆炸（512MB 单帧失败即旧路径缺陷现场）。zstd9 在随机数据上压缩 CPU 抵消
收益——激进压缩应配合可压缩画像由业务指定（接口已支持）。MB 级以上
payload 的绝对吞吐受同机自环 CPU 竞争限制，跨机部署为设计目标场景。

## 4. 端到端架构（2026-09-01 增强轮：与 write/read_object 全同构）

发送端（write_object 同构，尾部 WBQ → 连接发送队列）：调用线程 pickle.dump
→ 4MB 有界明文队列（背压）→ 压缩线程 CompressStage→CrcStage→BlockHeaderStage
→ 块记录直发（每块一帧，帧头+块头 41B 合并小发送，payload (ptr,len) 直入
连接发送队列，epoll 排空——无累积缓冲拷贝）。

接收端（read_object L3 同构，两段并行）：网络线程（server_loop 收窄）跨帧
块重组 → CRC 验证 → 压缩态记录入有界队列（64MB，满则阻塞——背压经 TCP 反压
发送方）∥ 消费线程（每流一根）出队 → 解压直写分段明文（32MB 段，resize 一次
落位零 realloc）→ END 对账 → 段序拼接（恰好一次全量）→ handler 按值 move 交付。
原实现单线程串行（解压时 wire 只靠内核缓冲吸收）+ plain 增长 realloc。

性能（自环全管线口径，3 轮中位，增强前→后；f64=solver 解向量画像）：

| 格子 | t_sender | t_recv | t_total | 有效吞吐 |
|---|---|---|---|---|
| 64MB f64 | 95→71ms | 217→122ms | 236→137ms | 272→467 MB/s |
| 512MB f64 | 3014→700ms | 11619→1204ms | 11771→1371ms | 43→373 MB/s |
| 64MB f64q | 170→160ms | 245→223ms | 263→241ms | 244→265 MB/s |
| 512MB f64q | 1348→1265ms | 1939→1695ms | 2056→1873ms | 249→273 MB/s |

总耗时 < 两端之和（重叠 20~40%，发送端三段流水成立）；512MB solver 画像
8.7×。512MB 绝对值受 5.8GB 测试机内存压力影响，足内存复测预期更好。
solver 动态链双方向已切流式（check 请求 _stream_call / member 响应
respond_writer）。残余杠杆（未做）：交付 assemble 拷贝（1×payload）可经
StreamRx 分段直供 PeerStreamBuffer 消除——待足内存数据支撑再立项。

## 5. 稳定性

`qa/stress/test_peer_rpc_stress.pyt`：混合 payload（空/1B/64KB/1MB/
4MB±1/16MB）× 6 轮流式 echo 校验 + 两连接并发收集圈 + respond_failure
错误路径；`qa/performance/test_peer_rpc_perf.pyt` 为性能矩阵 case
（大载荷档 64MB/512MB 需 PEER_RPC_PERF_FULL=1——-j6 稳定性套件下
大载荷并发会耗尽机器内存，连累同轮 case）。

**100 轮稳定性（2026-09-01，异步化最终代码 b93228c）**：ALL 100 ROUNDS
PASSED，每轮 169/169 case 零失败（16900 次执行），总耗时 2h31m。产物
.work/stability/20260901_095122/。首轮曾失败（perf 大载荷内存耗尽连带
同轮 case），加环境开关后全程稳定——非流式逻辑回归。
