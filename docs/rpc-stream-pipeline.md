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
- 接收端组装完整 payload 后走原 handler/PendingRpcMap 链（内存口径 =
  压缩态+明文各一份；反序列化仍由业务 pickle.loads）。
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

## 4. 稳定性

`qa/stress/test_peer_rpc_stress.pyt`：混合 payload（空/1B/64KB/1MB/
4MB±1/16MB）× 6 轮流式 echo 校验 + 两连接并发收集圈 + respond_failure
错误路径；`qa/performance/test_peer_rpc_perf.pyt` 为性能矩阵 case。
