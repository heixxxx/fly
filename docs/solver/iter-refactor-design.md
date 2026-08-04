# 迭代求解流程重构设计（常驻 task + 轻量 RPC）

> 设计日期：2026-08-04（经多轮讨论简化）
> 状态：方案已确认
> 核心简化：取消任务组概念，用 RPC 失败传播（超时/断连 + 2 次重连）驱动整组失败，无需 master 主动终止 task

## 一、目标

消除当前"每轮 task 链驱动迭代"的调度开销（占 22%）+ 数据流经 DB 的开销，改为 **nsd+1 个常驻 task + 轻量 RPC 直连**的迭代模型。

## 二、两个新增特性

### 特性 1：轻量 RPC 通信接口（独立交付）

业务级 API，允许 task 内通过 agent 实例与其他 worker 建立长连接并 RPC 通信。

**业务 API（Python）**：
```python
agent = get_agent()
# 建立/复用长连接，带重试（对端可能未就绪）
chan = agent.connect_peer(target_worker_id, retries=2, retry_interval=0.5)
resp_bytes = chan.rpc(request_bytes, timeout=30)   # 请求-响应（带 rpc_id）
chan.close()                                        # 主动关闭
```

**故障处理（内置）**：
- **启动时重试**：connect_peer 带 retries=2，每次间隔 retry_interval，覆盖"对端 task 未启动"的时序窗口
- **运行时重连**：RPC 断连后自动重连 2 次，覆盖网络瞬断
- **重连失败/超时**：rpc() 抛 `PeerDisconnected` / `RpcTimeout` 异常，调用方据以判断对端失败

**fly 序列化辅助**（payload 不强制格式，fly 提供 helper）：
```python
from fly import serialize_array, deserialize_array   # numpy ↔ bytes
```

**C++ 实现**：
- 新增**独立业务端口**（WorkerAgent 监听，独立于 DataServer，隔离清晰 + 生命周期独立）
- 新增消息类型：`PEER_RPC_REQUEST`（rpc_id + src_worker_id + payload bytes）、`PEER_RPC_RESPONSE`（rpc_id + payload bytes）
- PeerChannel 内部 `queue<response> + cv`，reactor 线程按 rpc_id 匹配入队
- payload 在 wire 上是裸 bytes（业务自定义序列化），不经 bitsery/pickle
- **主动退出监听接口**：`chan.close()` + WorkerAgent 业务端口在 task 结束/worker shutdown 时彻底关闭

**连接拓扑**：星型。compute 各自 connect 到 check worker 的业务端口。check accept nsd 条连接。

**业务端口注册**：check worker 启动业务端口后（OS 动态分配端口），将端口注册到 master（worker_registry 扩展或写 session_info 到 DB）。compute 通过 master 查询或读 DB 获取 check 的业务端口地址（带重试，覆盖 check 未就绪）。

### 特性 2：迭代流程重构（基于特性 1）

**当前模式**（task 链驱动）：
```
每轮: master 调度 nsd 个 compute task → 各写解到 DB → check task 读所有解 → 粗校正 → 提交下一轮 nsd 个 compute task
```

**重构模式**（常驻 task + RPC 直连）：
```
启动: 提交 nsd+1 个常驻 task（nsd compute + 1 check），各自占一个 worker
      compute 启动时 connect_peer(check) 带重试（check 可能晚起）
迭代: compute task 内 while 循环: solve → RPC 发解给 check → 收回复 → 继续/结束
      check task 内 while 循环: 收齐 nsd 份 → 粗校正/同步 → RPC 回复各 compute
结束: check 判定收敛 → 通知所有 compute done → 各 task 退出 → 关闭 chan/端口
```

**常驻 compute task 伪代码**：
```python
@as_task(requires=[f"sd_{sd}"])
def ras_graph_compute_daemon(db, sd, nsd):
    setup()                                          # 一次性 LDLT 分解
    info = db.read_object("__rasg__session_info")    # 角色分配（一次性）
    chan = agent.connect_peer(info["check_worker_id"], retries=2)  # 带重试
    step = 0
    while True:
        x = solve_local_step(step)
        try:
            resp = chan.rpc(serialize({"step": step, "x": x, "conv": conv_local}), timeout=30)
        except (RpcTimeout, PeerDisconnected):
            break   # RPC 失败 = check 失败 = 整次求解失败，退出
        result = deserialize(resp)
        if result["action"] == "done": break
        apply_correction(result["xc"]); step += 1
    chan.close()
```

**常驻 check task 伪代码**：
```python
@as_task
def ras_graph_check_daemon(db, nsd):
    info = db.read_object("__rasg__session_info")
    register_rpc_port(info)                          # 注册业务端口供 compute 查询
    chans = accept_all_compute_connections(info)    # accept nsd 条
    while True:
        try:
            contribs = [deserialize(chans[sd].rpc_wait(timeout=30)) for sd in range(nsd)]  # 收齐 nsd 份
        except (RpcTimeout, PeerDisconnected):
            break   # 某 compute 失败 = 整次求解失败，退出
        x_global = assemble(contribs)
        if all_converged(contribs):
            for sd: chans[sd].rpc(serialize({"action": "done"}))
            write_solution(); break
        corrected = coarse_correct(x_global)
        for sd: chans[sd].rpc(serialize({"action": "continue", "xc": corrected[sd]}))
    close_all(chans)
```

**coord 协调对象**（一次性，非每轮）：
solve_ras_graph 在 master 侧启动 nsd+1 个 worker 后，写 `__rasg__session_info` 到 DB：
```json
{"check_worker_id": <master 指派>, "compute_workers": [{"sd": 0, "worker_id": ...}, ...]}
```
各常驻 task 启动时读一次，得知角色。check 的业务端口地址由 check 启动后注册（compute 重试查询）。

## 三、失败传播（RPC 驱动，无需 master 介入取消）

**核心思想**：RPC 的超时 + 断连天然构成"心跳"——每次 RPC 既是数据交换也是存活确认。任一方停止响应，对方在 timeout 内感知，各自退出失败。

### 逐场景验证

| 失败场景 | 谁先感知 | 如何传播 | 结果 |
|---------|---------|---------|------|
| compute A 崩溃 | check 的 `rpc_wait(A)` 超时 | check 退出；其余 compute 的 `rpc(check)` 断连 → 退出 | 全部失败 ✅ |
| check 崩溃 | 所有 compute 的 `rpc(check)` 超时/断连 | 各 compute 退出 | 全部失败 ✅ |
| compute A solve 异常 | A 自身 TaskFailed | A 不再发 RPC → check 等 A 超时 → check 退出 → 其余 compute rpc(check) 断连 → 退出 | 全部失败 ✅ |
| 网络瞬断 | RPC 超时 | 内置 2 次重连，恢复则继续 | 瞬断恢复 ✅ |
| 永久断连 | 重连 2 次失败 | 按崩溃场景传播 | 全部失败 ✅ |

### 失败传播链（自动）
```
compute A 崩溃:
  → check 的 chan[A].rpc_wait() 超时（30s）
  → check task 退出（发 TaskFailed）
  → check 的业务端口关闭
  → 其余 compute 的 chan.rpc(check) 断连 → 重连 2 次失败 → 退出
  → master 收到所有 task 的 TaskFailed，整次求解失败
```

master 只需正常处理 TaskFailed（标记 FAILED），**不需要组感知、不需要主动取消、不需要任务组概念**。

### 与之前任务组方案的对比（为何简化）

| 维度 | 任务组方案（已放弃） | RPC 驱动方案（采用） |
|------|---------------------|-------------------|
| 任务组概念 | group_id + 组调度 + 组事务 | 不需要 |
| master 主动终止 task | TASK_CANCEL + cancel_flag + 检查点 | 不需要 |
| 同时调度 nsd+1 | 组原子调度 | compute 重试连接（容忍 check 晚起） |
| 失败传播 | master 介入取消 | RPC 超时/断连自动传播 |
| 新增机制数 | 4（group/cancel_flag/TASK_CANCEL/组调度） | 0（复用 RPC 超时/重连） |

RPC 驱动方案显著更简单，且失败传播是**去中心化**的（不依赖 master 感知+介入），更健壮。

## 四、容错语义

worker 崩溃 → RPC 断连 → 对端超时退出 → 整次求解失败。迭代间数据不持久化（save_to_db=False 的 temp 随进程消失），单次求解必须一次完成——这与当前模型一致（当前迭代中间崩溃同样是整次作废）。

RPC 驱动的失败传播让"整次求解失败"成为**显式、干净**的行为：所有 task 在 timeout 内退出，chan/端口关闭，worker 释放回 IDLE。相比当前"部分 task 被 master 重投后读不到 temp 卡死"的隐式故障，更健壮。

上层（solve_ras_graph）据 TaskFailed 决定是否重新发起整次求解。

## 五、收益预估（vs 当前 14.4s）

| 阶段 | 当前 | 重构后预估 | 说明 |
|------|------|-----------|------|
| 初始化（矩阵+setup） | ~7.1s | ~7.1s | 不变（LDLT 分解固有） |
| 调度间隙 | ~3.1s | **~0.3s** | 常驻 task 无每轮调度，仅启动时一次性 |
| COMPUTE（solve+数据交换） | ~1.6s + read_nb/write | solve ~1.6s + RPC 直连 ~0.2s | RPC 替代 DB read/write |
| COARSE 粗校正 | ~0.95s | ~0.5s | assemble 改 RPC 收集（无 DB read），write 改 RPC 发回 |
| 收尾 | ~0.3s | ~0.3s | 不变 |
| **预估总** | **14.4s** | **~10s** | **-30%** |

## 六、实施顺序

1. **特性 1：轻量 RPC 接口**（独立交付，验证长连接 + RPC 往返延迟 + 重连）
2. **特性 2：迭代重构**（基于特性 1，重写 ras_graph 的 compute/check 为常驻 task）

每步独立验证。特性 1 完成后可单独测 RPC 延迟（预期 ~1ms vs 当前 DB ~5ms）+ 重连行为，确认收益再推进。

## 七、待设计细节（实施时细化）

- PEER_RPC 消息的 framing 格式（长度前缀 + rpc_id + payload）
- 业务端口的 accept 线程模型（复用 reactor 还是独立线程池）
- 业务端口注册/查询机制（worker_registry 扩展 vs DB session_info）
- connect_peer 的重试参数（retries/retry_interval 默认值，覆盖 check 启动延迟）
- RPC 超时/断连异常的 Python 类设计（RpcTimeout / PeerDisconnected）
- 序列化辅助的具体 API（serialize_array 是否支持指定 dtype/shape）
- check 的 accept 逻辑（如何关联连接到 sd_id，rpc_wait 的收齐语义）
