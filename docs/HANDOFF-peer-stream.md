# HANDOFF — PeerRpc 真流式读端（换机续作交接）

> 2026-09-01。主线状态：全部合入 main（`b48b626`），qa/solver 16/16、
> 全量 QA 169/169。本文档供换机继续开发/debug 使用，事项完结后可删。

## 1. 已落地（b48b626 及此前提交）

- **真流式读端**：`PeerStreamReader`（单块/流式双形态）——请求 START 即
  派发 handler，业务 `pickle.load(reader)` 拉动 `make_block_read_pipeline`
  （CrcVerifyStage → DecompressStage，与 read_object 共享 Stage）边收边
  解压边反序列化。EOF 仅在 END 三重对账（总量/块数/consumed）通过后放行，
  失败/断连读时抛错零容忍。已删除中间版本的消费线程/分段缓冲/END 拼接
  （「收齐才反序列化」= 大 payload 2× 差距根源）。
- **五处缺陷修复**（详见 DOC_CHANGELOG 2026-09-01 (3)）：compat read_all
  误装流式变体 / GIL 释放态构造 bytes / readinto 拒 memoryview / END 处理
  缺 continue 跌落吞 START / `srv_` 裸指针悬垂（PeerRpcServer 改共享所有
  权 + enable_shared_from_this）。
- **准则**：DEVELOPMENT_GUIDELINES.md 新增 §16 所有权与指针规范。
- solver 动态链：双方向流式 + 显式 none 压缩（f64 解向量近随机）。

## 2. 遗留观察项（换机后优先关注）

**sd9 偶发挂起**：`test_golden_n50_sd9` 曾 12 轮出现 1 次 TIMEOUT（62s）。
- 修复后 soak 46+ 轮（sd9/r30 交替）零复现——大概率已被 ③readinto/④END
  continue 修复覆盖，但未归因到单一行，**保留观察**。
- 若复现，诊断路径（打点已在树内，DBG 级）：check 侧
  `[R-START]/[R-END]`（收到并派发的响应流）与 member 侧
  `[W-START]/[W-END]`（发出的响应流）按 rpc_id 对账——缺失的 R-START 即
  丢帧点；check 的 server_loop 若 epoll 空转则确认帧未到达。
- ~~已知连带缺陷：check 的收集圈 `fut.result()` 超时为 0（无限等），会绕过
  30s 收集死线直接挂死~~ ✅ **已修复**（2026-09-02 批次 3：收集圈等待 bounded
  于 30s deadline，新增 `rasgd_collect_deadline` subcase 实证，见 DOC_CHANGELOG）。

## 3. 待办：基准复测（流式读端性能未出数）

> 2026-09-03 状态：旧机已补单帧基线全档位（d1062f1：64MB 流式 218 vs 单帧
> 200 MB/s；512MB 流式稳定完成 41 MB/s，写缓冲压力现场）——旧机内存压力下
> 数据与本节验收线不可比，**新机（≥16GB）复测仍未做**。

收齐交付版的基准（自环全管线口径）：64MB f64 467 MB/s / 512MB f64
373 MB/s / 远端 read_object 727-885 MB/s。真流式版（业务拉动）预期 ≥ 远
端读量级，**换机后需复测**：复现脚本形态见 DOC_CHANGELOG 2026-09-01 (3)
的验证段（NOT_READY 重试 + 同 conn 双流 + 远端读对照），脚本本体已随
`.work/` 清理，按 `qa/performance/test_peer_rpc_perf.py` 的 member/check
骨架 + `peer_rpc_recv_request_stream`/`peer_stream_response_reader` 重写
即可。验收线：none 压缩 512MB ≥ 700 MB/s。

## 4. 机器约束提示

原调试机为 5.8GB 内存 WSL2（4 核）：512MB 档基准内存压力敏感（方差大），
`none` 压缩在该机 512MB 档曾因突发打爆内存带宽而劣化——新机（≥16GB）
预期 none 全面优于 lz4。sd9 这类 9-member 并发 case 在 4 核上方差大。
