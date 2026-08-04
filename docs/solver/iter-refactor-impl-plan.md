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
  - 独立 accept 线程（或复用 IOThreadPool）+ ConnectionManager
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

### 步骤 2.1：solve_ras_graph 改造（启动 nsd+1 + 创建 group）
- `ras_graph.py` solve_ras_graph：
  - n_workers = nsd + 1（一个 check 无 sd 属性 + nsd 个 compute 带 sd 属性）
  - 创建 PeerChannelGroup（master 侧，随 task 参数传递）
  - 提交 ras_graph_check_daemon（无 requires）+ nsd 个 ras_graph_compute_daemon（requires=[sd_X]）
  - master 随机调度，check 落任意 worker，compute 落对应 sd worker
- coord task 保留（预构建矩阵/分区/coarse），但不再提交迭代 task（由 daemon 驱动）
- **验证**：编译 + 基本启动

### 步骤 2.2：ras_graph_compute_daemon（常驻 while task）
- 新建 compute daemon task：
  - setup()（一次性 LDLT 分解，复用现有 ras_graph_setup 逻辑）
  - chan = group.connect(db)（wait_obj + connect_peer）
  - while True：solve_local_step → chan.rpc(check, {step, x, conv}) → 收 reply → continue/done/error
  - 三层失败 catch（RpcTimeout/PeerDisconnected/PeerFailed）→ break
  - chan.close()
- 复用现有 setup 的 BFS/rank filter/LDLT 逻辑（ras_graph_setup 抽取为可复用函数）
- **验证**：单 compute 启动 + RPC 连通

### 步骤 2.3：ras_graph_check_daemon（常驻 while task + 主动 fan-out）
- 新建 check daemon task：
  - listener = group.listen(db)（绑端口 + write temp）
  - chans = listener.accept_n(nsd)
  - while True：
    - 收齐 nsd 份（chans[i].rpc_wait，带 timeout）
    - 失败 catch → **主动 fan-out** notify_failure 其余 → break
    - assemble + coarse_correct（复用 _apply_coarse_correction 逻辑）
    - 收敛 → rpc done 所有 + write sol → break
    - 否则 → rpc continue + xc 各 compute
  - check 自身异常 → fan-out notify 所有
  - listener.close()
- **验证**：端到端 n=50 小规模收敛

### 步骤 2.4：清理旧 task 链代码
- 保留 ras_graph_coord（预构建）+ setup（抽取）+ 新 daemon
- 移除/废弃 ras_graph_compute（旧每轮 task）+ ras_graph_check（旧提交下一轮）
- 保留 fallback（omega != "coarse" 的非 daemon 路径，或全部切 daemon）
- **验证**：编译 + 小规模收敛

### 步骤 2.5：特性 2 验证
- n=1000 nsd=4 coarse 性能对比（预期 14.4s → ~10s）
- 精度一致性（rel_err 3.37e-12）
- stability 50 轮
- 失败传播测试（杀一个 compute worker，确认整组失败）
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
