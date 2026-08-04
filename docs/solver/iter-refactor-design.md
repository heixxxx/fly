# 迭代求解流程重构设计（常驻 task + 轻量 RPC + 任务组）

> 设计日期：2026-08-04
> 状态：方案讨论确认中
> 依据：多轮架构调研 + 实测数据 + 与用户的设计讨论

## 一、目标

消除当前"每轮 task 链驱动迭代"的调度开销（占 22%）+ 数据流经 DB 的开销，改为 **nsd+1 个常驻 task + 轻量 RPC 直连**的迭代模型。

## 二、三个新增特性（按依赖顺序）

### 特性 1：轻量 RPC 通信接口（独立交付）

业务级 API，允许 task 内通过 agent 实例与其他 worker 建立长连接并 RPC 通信。

**业务 API（Python）**：
```python
agent = get_agent()
chan = agent.connect_peer(target_worker_id)       # 建立/复用长连接，返回 PeerChannel
resp_bytes = chan.rpc(request_bytes, timeout=30)  # 请求-响应（带 rpc_id），简化使用
chan.close()                                       # 主动关闭
```

**fly 序列化辅助**（payload 不强制格式，fly 提供 helper）：
```python
from fly import serialize_array, deserialize_array   # numpy ↔ bytes
```

**C++ 实现**：
- 新增业务端口（WorkerAgent 监听一个业务端口，独立于 DataServer）
- 新增消息类型：`PEER_RPC_REQUEST`（含 rpc_id + src_worker_id + payload bytes）、`PEER_RPC_RESPONSE`（含 rpc_id + payload bytes）
- PeerChannel 内部 `queue<response> + cv`，reactor 线程按 rpc_id 匹配入队
- payload 在 wire 上是裸 bytes（业务自定义序列化），不经 bitsery/pickle
- **提供主动退出监听接口**：`chan.close()` + WorkerAgent 的业务端口在 task 结束/worker shutdown 时彻底关闭

**端口方案**：新增独立业务端口（不复用 DataServer）。理由：
- 业务消息与数据消息语义不同（控制 vs 大块数据），隔离更清晰
- DataServer 的两段式协议（DataResponseProtocol）专为大数据，业务 RPC 是小消息请求-响应，复用会增加路由复杂度
- 独立端口便于生命周期管理（求解结束后关闭）

**连接拓扑**：星型。compute worker 各自 connect 到 check worker 的业务端口。check worker accept nsd 条连接。

### 特性 2：任务组（Task Group）— 保证 nsd+1 个 task 同时调度

**问题**：nsd 个 compute + 1 个 check 是 nsd+1 个常驻 task。若逐个调度，可能前几个占用了 worker 但凑不齐 nsd+1，导致迭代无法启动（check 等不到所有 compute 连接，compute 等不到 check 响应）。

**设计**：新增"任务组"概念。同一组内的 task **必须同时调度**——要么一次性分配到 nsd+1 个 worker，要么都不分配（等足够 worker 空闲后一起调度）。

**调度规则**：
- 组内 task 的 capability 取**并集**或**最大需求**，用于匹配 worker
- 调度器在 `schedule_all_available` 中：若某组所需 worker 数（如 nsd+1）> 当前空闲 worker 数，**跳过整组**（不部分调度），等待更多 worker 空闲
- 一旦空闲 worker ≥ 组需求，一次性把组内所有 task 分配出去

**实现要点**：
- TaskSubmitMessage 增加 `group_id`（可选，同组 task 共享）
- DependencyGraph / TaskScheduler 增加组感知：组内 task 原子就绪 + 原子调度
- 组的最小需求（worker 数）= 组内 task 数

**check 选定**：master 随机指派（无网络拓扑检测）。任务组调度时，master 把 nsd+1 个 task 一起发出，落到哪 nsd+1 个 worker 由调度器决定。各 task 启动后读协调对象（DB 一次性写入）得知自己的角色 + 其他成员地址。

### 特性 3：迭代流程重构（基于特性 1+2）

**当前模式**（task 链驱动）：
```
每轮: master 调度 nsd 个 compute task → 各写解到 DB → check task 读所有解 → 粗校正 → 提交下一轮 nsd 个 compute task
```
每轮 nsd+1 个 task 的提交/调度/派发，数据经 DB write/read/ObjectCache。

**重构模式**（常驻 task + RPC 直连）：
```
启动: 任务组一次性调度 nsd+1 个常驻 task（nsd compute + 1 check），各占一个 worker
迭代: compute task 内 while 循环: solve → RPC 发解给 check → 收 check 回复 → 继续/结束
      check task 内 while 循环: 收齐 nsd 份 → 粗校正/同步 → RPC 回复各 compute
结束: check 判定收敛 → 通知所有 compute done → 各 task 退出 → 关闭业务端口
```

**常驻 compute task 伪代码**：
```python
@as_task(requires=[f"sd_{sd}"], group="solve_session_{id}")
def ras_graph_compute_daemon(db, sd, nsd):
    setup()                                          # 一次性 LDLT 分解
    info = db.read_object("__rasg__session_info")    # 角色分配（一次性）
    chan = agent.connect_peer(info["check_worker_id"])
    step = 0
    while True:
        x = solve_local_step(step)                   # 本地 LDLT solve + omega
        resp = chan.rpc(serialize({"step": step, "x": x, "conv": conv_local}))
        result = deserialize(resp)
        if result["action"] == "done": break
        apply_correction(result["xc"]); step += 1
    chan.close()
```

**常驻 check task 伪代码**：
```python
@as_task(group="solve_session_{id}")
def ras_graph_check_daemon(db, nsd):
    info = db.read_object("__rasg__session_info")
    chans = accept_all_compute_connections(info)    # accept nsd 条
    while True:
        contribs = [deserialize(chans[sd].rpc_wait()) for sd in range(nsd)]  # 收齐 nsd 份
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
{"check_worker_id": <master 指派>, "check_host": ..., "check_rpc_port": ...,
 "compute_workers": [{"sd": 0, "worker_id": ..., "host": ...}, ...]}
```
各常驻 task 启动时读一次，得知角色 + 连接目标。

## 三、收益预估（vs 当前 14.4s）

| 阶段 | 当前 | 重构后预估 | 说明 |
|------|------|-----------|------|
| 初始化（矩阵+setup） | ~7.1s | ~7.1s | 不变（LDLT 分解固有） |
| 调度间隙 | ~3.1s | **~0.3s** | 常驻 task 无每轮调度，仅启动时一次性 |
| COMPUTE（solve+数据交换） | ~1.6s + read_nb/write | solve ~1.6s + RPC 直连 ~0.2s | RPC 替代 DB read/write |
| COARSE 粗校正 | ~0.95s | ~0.5s | assemble 改 RPC 收集（无 DB read），write 改 RPC 发回 |
| 收尾 | ~0.3s | ~0.3s | 不变 |
| **预估总** | **14.4s** | **~10s** | **-30%** |

主要收益：调度间隙 3.1→0.3s（-2.8s）+ 数据交换 DB→RPC（-1s 量级）。

## 四、风险与设计约束

### 容错（已确认可接受）
worker 崩溃 → master 心跳感知 → 重投常驻 task（从头 setup + 重新迭代）。但**迭代间数据不持久化**（save_to_db=False 的 temp 随进程消失），单次求解必须一次完成。这与当前模型一致（当前模型迭代中间崩溃同样是整次作废）。

常驻模型额外要求：check 的 rpc_wait 需带 timeout，检测 compute 崩溃后中止整次求解（避免永久阻塞）。

### task 内通信的线程模型
task 在 Python 主线程阻塞 while 循环，reactor 线程收 PEER_RPC_RESPONSE → 入 PeerChannel 的 queue → `chan.rpc` 从 queue 取（cv 唤醒）。需保证 reactor 线程能并发处理业务消息（当前 reactor 单线程，业务 RPC 响应小且快，单线程可承载）。

### 业务端口生命周期
- task 启动时 `agent.connect_peer` / accept
- task 结束（while 退出）时 `chan.close()`
- worker shutdown 时业务端口彻底关闭（提供 stop 接口）

## 五、实施顺序

1. **特性 1：轻量 RPC 接口**（独立交付，验证长连接 + RPC 往返延迟）
2. **特性 2：任务组**（调度器改造，验证 nsd+1 task 同时调度）
3. **特性 3：迭代重构**（基于 1+2，重写 ras_graph 的 compute/check 为常驻 task）

每步独立验证。特性 1 完成后可单独测 RPC 延迟（预期 ~1ms vs 当前 DB ~5ms），确认收益再推进。

## 六、待确认/待设计细节（实施时细化）

- PEER_RPC 消息的 framing 格式（长度前缀 + rpc_id + payload）
- 业务端口的 accept 线程模型（复用 reactor 还是独立线程池）
- 任务组在 DependencyGraph 中的表示（group_id 字段 + 组就绪判定）
- check 的 rpc_wait 超时策略 + compute 崩溃后的中止协议
- 序列化辅助的具体 API（serialize_array 是否支持指定 dtype/shape）
