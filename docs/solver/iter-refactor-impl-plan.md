# 迭代重构完整实施计划（特性 1 + 特性 2 + 预研）

> 制定日期：2026-08-04
> 依据：iter-refactor-design.md（已确认设计）+ 15 条已确认决策
> 约束：逐步实施，每步独立验证，零功能回归

## 决策遵循清单（实施时逐条对照）

| # | 决策 | 实施体现 |
|---|------|---------|
| 1 | 独立业务端口（不复用 reactor/DataServer） | PeerRpcServer 独立 listen 端口 |
| 2 | RPC 式（请求-响应带 id） | PeerRpcRequest/ResponseMessage + PendingRpcMap |
| 3 | fly 提供序列化辅助 | serialize_array/deserialize_array |
| 4 | 星型拓扑 | compute 各 connect check |
| 5 | 单次求解一次完成（无迭代间容错） | 失败即整次作废 |
| 6 | check 随机指派 | master 调度 nsd+1 task，check 无 sd 属性 |
| 7 | RPC 失败传播替代任务组 | 无 group_id，靠 RPC 超时/断连/notify |
| 8 | 内置两次重连 | PeerRpcServer connect 带 retries=2 |
| 9 | 主动失败通知优先 | check fan-out notify_failure |
| 10 | PeerChannelGroup 可 task 参数传递 | pickle 占位符 `__fly_rpc__:{group_id}` |
| 11 | 内部随机唯一名称 | uuid group_id |
| 12 | listen/connect 内置 DB temp + wait_obj | listen 写 temp，connect wait_obj 读 |
| 13 | compute/check 常驻 while task | while 循环驱动迭代 |
| 14 | nsd+1 worker | solve_ras_graph 启 nsd+1 |
| 15 | check 端口信息 DB temp + wait_obj | PeerChannelGroup.listen 内部实现 |

---

## 特性 1：PeerChannelGroup 轻量 RPC 接口

### 步骤 1.1：C++ 消息类型
- `message_types.h`：加 PEER_RPC_REQUEST=50/PEER_RPC_RESPONSE=51 + is_valid 上界 49→51
- 新增 PeerRpcRequestMessage（header_ + rpc_id + src_worker_id + payload CMString）、PeerRpcResponseMessage（header_ + rpc_id + status + payload）
- 更新 message_protocol_test.cpp 上界断言
- **验证**：编译 + message_protocol_test

### 步骤 1.2：C++ PeerRpcServer（独立端口）
- 新建 `src/agent/cpp/peer_rpc_server.{h,cpp}`
- PeerRpcServer 类：
  - listen(port=0) 绑定独立业务端口（OS 分配）
  - 独立 accept 线程 + ConnectionManager（计划期曾考虑复用 IOThreadPool，该类已于 2026-08-16 作为死代码删除）
  - on_request 回调路由（收 PeerRpcRequestMessage → 业务处理）
  - send_response(conn_id, rpc_id, status, payload)
  - connect_peer(host, port, retries=2) → conn_id（客户端，带重连）
  - send_request(conn_id, msg)
  - **stop()**：关闭端口 + 清理所有连接（主动退出监听接口）
- `src/agent/cpp/BUILD`：加 peer_rpc_server 源文件
- **验证**：编译通过

### 步骤 1.3：WorkerAgent 集成
- `worker_agent.h`：加 `CMUniquePtr<PeerRpcServer> peer_rpc_server_` + rpc_port_ + 方法
- `worker_agent.cpp`：start() 创建+启动 PeerRpcServer；stop() 关闭
- connect_peer(host, port) / send_peer_rpc(conn_id, payload, timeout) / notify_failure(conn_id, reason)
- PendingRpcMap<uint64_t, PendingPeerRpc> 管理请求-响应
- `agent_export.cpp`：FLY_EXPORT 新方法
- **验证**：编译 + agent 单测

### 步骤 1.4：Python PeerChannelGroup 封装
- `agent.py`：
  - PeerChannelGroup（uuid group_id，pickle 占位符 `__fly_rpc__:{group_id}`）
  - listen(db)：PeerRpcServer 绑端口 + write_object(temp, {host, rpc_port}) → PeerListener
  - connect(db)：wait_obj 读地址 + connect_peer → PeerChannel
  - PeerChannel.rpc(payload, timeout) / notify_failure(reason) / close()
  - PeerListener.accept_n(n) / close()
  - serialize_array / deserialize_array（numpy ↔ bytes）
- task 参数序列化：_serialize_args 加 `__fly_rpc__:` 前缀处理（仿 `__fly_db__:`）
- **验证**：install + Python RPC 往返单测

### 步骤 1.5：特性 1 验证
- RPC 延迟 benchmark（预期 ~1ms vs DB ~5ms）
- notify_failure 传播测试
- 断连重连测试
- 全量单测 + QA 零回归
- **提交**

---

## 特性 2：迭代重构（基于特性 1）

特性 2 包含三个子优化：常驻 task 迭代重构 + 分块矩阵 setup + 树形归约。三者协同：

### 步骤 2.1：分块矩阵 setup 优化（coord 预分块发布）

**问题**（实测）：当前每 worker 全量加载矩阵（130MB）+ 各自 BFS 扩展(633ms) + rank filter(52ms) + adjacency 构建(85ms)。这些步骤都依赖全量矩阵，但每子域只需要自己的子块。

**优化**：coord task（master 侧，单进程）一次性预分块：
- coord 构建全量 adjacency（一次 argsort）+ 对每子域做 BFS 扩展 + rank filter
- 提取每子域的 `{local_indices, a_rows, a_cols, a_vals, b_orig, outside_coeffs, neighbor_*}` 并发布到 DB（`__rasg__subdomain_{sd}`，save_to_db=False temp）
- compute daemon 启动时直接读自己的子域数据（跳过全量矩阵加载 + BFS + rank filter），只做 LDLT 分解（固有，不可省）

**收益**：
- 消除每 worker 的全量矩阵加载（130MB × 4 → coord 一次 130MB）+ adjacency 重复构建（85ms × 4 → coord 一次）
- BFS 扩展在 coord 单进程做（nsd 次，但共享已构建的 adjacency，避免每 worker 重复 argsort）
- compute daemon 的 setup 只剩 LDLT 分解（~1171ms，固有）

**注意**：coord 的 BFS 仍是 Python 循环（633ms × nsd），但相比 4 worker 各自全量加载 + 重复 BFS，coord 复用 adjacency 更优。后续可进一步用 numpy 向量化 BFS（csr index），但非本次必需。

**验证**：setup 时间对比（worker 侧 setup 从 ~1.9s 降到 ~1.2s 仅 LDLT）。

### 步骤 2.2：solve_ras_graph 改造（启动 nsd+1 + 创建 group + coord 预分块）
- n_workers = nsd + 1（一个 check 无 sd 属性 + nsd 个 compute 带 sd 属性）
- 创建 PeerChannelGroup（master 侧，随 task 参数传递）
- coord task 增强：原 coord 逻辑 + 步骤 2.1 的预分块发布
- 提交 ras_graph_check_daemon（无 requires）+ nsd 个 ras_graph_compute_daemon（requires=[sd_X]）
- master 随机调度，check 落任意 worker，compute 落对应 sd worker
- **验证**：编译 + 基本启动

### 步骤 2.3：ras_graph_compute_daemon（常驻 while task）
- setup：读预分块子域数据（步骤 2.1）→ LDLT 分解（固有）
- chan = group.connect(db)（wait_obj + connect_peer）
- while True：solve_local_step → chan.rpc(check, {step, x, conv}) → 收 reply → continue/done/error
- 三层失败 catch（RpcTimeout/PeerDisconnected/PeerFailed）→ break
- chan.close()
- **验证**：单 compute 启动 + RPC 连通

### 步骤 2.4：ras_graph_check_daemon（常驻 while task + 树形归约 + 主动 fan-out）
- listener = group.listen(db)（绑端口 + write temp）
- chans = listener.accept_n(nsd)
- while True：
  - **收齐 nsd 份**：当前设计是串行 `for i: chans[i].rpc_wait()`。优化为**并发收齐**（所有 chan 同时 rpc_wait，非串行逐个）—— 这不是严格树形归约，但消除了串行等待。真正的树形归约（compute 间两两配对归约再上报 check）作为后续可选优化（见步骤 2.6）
  - 失败 catch → **主动 fan-out** notify_failure 其余 → break
  - assemble + coarse_correct（复用 _apply_coarse_correction 逻辑）
  - 收敛 → rpc done 所有 + write sol → break
  - 否则 → rpc continue + xc 各 compute
- check 自身异常 → fan-out notify 所有
- listener.close()
- **验证**：端到端 n=50 小规模收敛

### 步骤 2.5：清理旧 task 链代码
- 保留 ras_graph_coord（增强预分块）+ 新 daemon
- 移除/废弃 ras_graph_compute（旧每轮 task）+ ras_graph_check（旧提交下一轮）+ ras_graph_setup（合并进 coord 预分块）
- **验证**：编译 + 小规模收敛

### 步骤 2.6（可选）：树形归约优化
**背景**：当前 check 串行/并发收齐 nsd 份。nsd 大时（16+），check 成为瓶颈。树形归约让 compute 间两两配对归约部分和，最终只 1 份上报 check。

**前提**：特性 1 的 PeerChannelGroup 已落地（RPC 直连 ~1ms/步，使树形可行——之前 DB 通路 12ms/步 时树形不划算）。

**设计**（基于 allreduce-log-nsd-feasibility.md）：
- compute 间按 log₂(nsd) 步配对，每步 PeerChannelGroup RPC 交换部分和
- 最终每子域的归约结果上报 check
- check 的 assemble 从读 nsd 份降到读 1 份（或归约后的少量数据）

**实施时机**：nsd ≤ 8 时 check 并发收齐已够快（步骤 2.4），树形收益有限。nsd ≥ 16 时再实施。本次作为可选，视步骤 2.5 实测结果决定。

### 步骤 2.7：特性 2 验证
- n=1000 nsd=4 coarse 性能对比（预期 14.4s → ~10s）
- 精度一致性（rel_err 3.37e-12）
- stability 50 轮
- 失败传播测试（杀一个 compute worker，确认整组失败）
- 分块 setup 收益验证（worker setup 时间对比）
- 全量 QA 零回归
- **提交**

---

## 预研：分布式迭代法（PCG/CG）在 fly 上的实现路径

### 步骤 3.1：PCG 原型设计
- 基于 PeerChannelGroup，设计分布式 PCG：
  - 矩阵按行分布到 nsd 个 worker（无 check，纯迭代）
  - 每迭代：local SpMV + halo RPC + dot product（RPC 归约）
  - 无 Allreduce 原语 → dot product 用 check 式归约（单 worker 收集标量求和广播）
- 评估：fly 上 PCG 的通信占比（预期 ~30%，vs 当前 DB 93%）
- **产出**：PCG 实现路径文档 + 可行性结论

### 步骤 3.2：（可选）PCG 原型实现 + 对比
- 若路径可行，实现最小 PCG 原型（n=1000 Poisson）
- 对比 PCG vs RAS（迭代数、时间、精度）
- **产出**：性能对比数据

---

## 风险与守护

| 风险 | 守护 |
|------|------|
| is_valid 上界漏改 | message_protocol_test 同步 + 手动验证 |
| PeerRpcServer 生命周期（start/stop） | WorkerAgent stop 显式关闭，求解结束清理 |
| 常驻 task 阻塞 worker（不返回） | poll_task 同步执行，task 不返回则 worker 不处理后续——这是预期行为（常驻） |
| RPC 失败传播遗漏场景 | 逐场景测试（compute 崩溃/check 崩溃/solve 异常/断连） |
| 特性 2 精度回归 | n=1000 rel_err 对比，stability 50 轮 |
| 旧 task 链与新 daemon 并存混乱 | 清理旧代码（步骤 2.4），不留双路径 |

## 实施纪律
- 每步独立编译 + 验证 + 提交
- 任一步回归立即定位修复，不留累积
- 特性 1 完整验证后才进特性 2
- 特性 2 端到端验证后才进预研
