# 迭代求解流程重构设计（常驻 task + 轻量 RPC）

> 设计日期：2026-08-04（经多轮讨论简化）
> 状态：方案已确认
> 核心简化：取消任务组概念，用 RPC 失败传播（超时/断连 + 2 次重连）驱动整组失败，无需 master 主动终止 task

## 一、目标

消除当前"每轮 task 链驱动迭代"的调度开销（占 22%）+ 数据流经 DB 的开销，改为 **nsd+1 个常驻 task + 轻量 RPC 直连**的迭代模型。

## 二、两个新增特性

### 特性 1：轻量 RPC 通信接口（独立交付）

业务级 API，允许 task 内通过 agent 实例与其他 worker 建立长连接并 RPC 通信。

#### PeerChannelGroup：可传递的 channel 工厂（核心抽象）

将地址交换 + listen/connect + RPC + 失败传播封装为一个**可 pickle 的轻量对象**，通过 task 参数传递。业务代码完全不接触 DB 协调细节、不约定字符串名。

```python
# coord/master 侧创建一次
group = agent.create_channel_group()
# 内部生成唯一 id（uuid），作为 DB temp 对象名的后缀（避免并发求解会话冲突）

# 通过 task 参数传递（group 随 task pickle 序列化到各 worker）
submit_task(check_daemon, group=group)
for sd in range(nsd):
    submit_task(compute_daemon, group=group, sd=sd)
```

**check 侧（监听方）**：
```python
def check_daemon(db, group):
    listener = group.listen(db)     # 内部：绑定端口 + write_object(temp, {host,port})
    chans = listener.accept_n(nsd)  # 接受 nsd 个连接 → {sd_or_id: PeerChannel}
    # ... RPC 迭代 ...
    listener.close()

# compute 侧（连接方）
def compute_daemon(db, group, sd):
    chan = group.connect(db)        # 内部：wait_obj 读地址 + connect_peer 长连接
    resp = chan.rpc(req, timeout=30)
    chan.close()
```

**PeerChannelGroup 设计**：
```python
class PeerChannelGroup:
    """可 pickle 的 channel 工厂。仅含唯一 id，无连接状态（连接状态在 worker 本地建）。"""
    def __init__(self, group_id: str = None):
        self.group_id = group_id or str(uuid.uuid4())
    # 内部 temp 对象名 = f"__fly_chan_{self.group_id}"
    def listen(self, db) -> PeerListener: ...   # db 依赖传入
    def connect(self, db) -> PeerChannel: ...   # db 依赖传入
    # __getstate__/__setstate__ 仅 pickle group_id（轻量，随 task 参数传递）
```

**为什么能随 task 参数传递**：
- task 参数（args_）经 cloudpickle 序列化（executor.py:23），可 pickle 对象即可
- PeerChannelGroup 只含一个 uuid 字符串，pickle 后几十字节
- worker 侧反序列化拿到同 group_id 的实例 → listen/connect 用 `__fly_chan_{group_id}` 作为 DB temp 名 → check 和 compute 天然匹配（同一个 id）

**业务完全无感知**：不传字符串名、不写 write_object/wait_obj、不管 host/port。只创建 group → 传参 → listen/connect。

#### 故障处理（三层检测，冗余覆盖）

失败信号按响应速度排序，任一触发即视为对端失败：

| 层 | 信号 | 触发条件 | 响应速度 | 机制 |
|----|------|---------|---------|------|
| 1（最快）| **主动告知** | 对端正常退出（solve 异常/done/错误），退出前发 `notify_failure` | 立即 | `chan.notify_failure(reason)` → 对端 rpc 立即返回 error |
| 2 | **断连** | 对端进程死/网络断，TCP RST | 秒级 | 内置 2 次重连，仍失败抛 `PeerDisconnected` |
| 3（兜底）| **超时** | 对端无响应（卡死/慢） | timeout（默认 30s） | rpc(timeout) 到期抛 `RpcTimeout` |

- **正常运行时重连**：RPC 断连后自动重连 2 次（覆盖网络瞬断），仍失败才抛 PeerDisconnected
- **重连/超时/主动告知** 三者均导致 task 退出失败，由调用方（while 循环）catch 异常后 break

**fly 序列化辅助**（payload 不强制格式，fly 提供 helper）：
```python
from fly import serialize_array, deserialize_array   # numpy ↔ bytes
```

**C++ 实现**：
- 新增**独立业务端口**（WorkerAgent 监听，独立于 DataServer，隔离清晰 + 生命周期独立）
- 新增消息类型：
  - `PEER_RPC_REQUEST`（rpc_id + src_worker_id + payload bytes）
  - `PEER_RPC_RESPONSE`（rpc_id + status[ok/error] + payload bytes）— status=error 用于主动告知
  - 断连由 reactor 的 on_disconnect 检测
- PeerChannel 内部 `queue<response> + cv`，reactor 线程按 rpc_id 匹配入队
- payload 在 wire 上是裸 bytes（业务自定义序列化），不经 bitsery/pickle
- **主动退出监听接口**：`chan.close()` + WorkerAgent 业务端口在 task 结束/worker shutdown 时彻底关闭

**连接拓扑**：星型。compute 各自 connect 到 check worker 的业务端口。check accept nsd 条连接。地址交换由 PeerChannelGroup 的 listen/connect 内部用 DB temp + wait_obj 自动完成（复用 `_wait_for_objects`）。

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

**常驻 compute task 伪代码**（group 作为 task 参数传入）：
```python
@as_task(requires=[f"sd_{sd}"])
def ras_graph_compute_daemon(db, group, sd, nsd):
    setup()                                          # 一次性 LDLT 分解
    chan = group.connect(db)                         # 内部 wait_obj + connect_peer
    step = 0
    while True:
        x = solve_local_step(step)
        try:
            resp = chan.rpc(serialize({"step": step, "x": x, "conv": conv_local}), timeout=30)
            result = deserialize(resp)
        except (RpcTimeout, PeerDisconnected, PeerFailed) as e:
            break   # RPC 失败 = check 失败或网络不可恢复，退出
        if result["action"] == "done": break
        apply_correction(result["xc"]); step += 1
    chan.close()
```

**常驻 check task 伪代码**（group 作为 task 参数传入）：
```python
@as_task
def ras_graph_check_daemon(db, group, nsd):
    listener = group.listen(db)                      # 内部绑定端口 + write_object(temp)
    chans = listener.accept_n(nsd)                   # {sd_or_idx: PeerChannel}
    while True:
        try:
            # 收齐 nsd 份；任一超时/断连/失败 → 检测到不可恢复错误
            contribs = {}
            for i in range(nsd):
                resp = chans[i].rpc_wait(timeout=30)   # 等该 compute 发来本轮解
                contribs[i] = deserialize(resp)
        except (RpcTimeout, PeerDisconnected, PeerFailed) as e:
            # ★ 主动 fan-out 通知其余存活的 compute 失败（不等超时连锁）
            for i in range(nsd):
                try: chans[i].notify_failure(f"peer failed: {e}")
                except Exception: pass
            break
        x_global = assemble(contribs)
        if all_converged(contribs):
            for i in range(nsd): chans[i].rpc(serialize({"action": "done"}))
            write_solution(); break
        try:
            corrected = coarse_correct(x_global)
            for i in range(nsd):
                chans[i].rpc(serialize({"action": "continue", "xc": corrected[i]}))
        except Exception as e:
            # ★ check 自身错误，主动 fan-out 通知所有 compute
            for i in range(nsd):
                try: chans[i].notify_failure(f"check error: {e}")
                except Exception: pass
            break
    listener.close()
```

**coord 协调**（一次性）：
solve_ras_graph 在 master 侧启动 nsd+1 个 worker 后：
```python
group = agent.create_channel_group()   # 创建可传递的 channel 工厂
submit_task(check_daemon, group=group)
for sd in range(nsd):
    submit_task(compute_daemon, group=group, sd=sd)
```
group 随 task 参数 pickle 传递到各 worker。check/compute 用同一个 group_id 的 listen/connect 自动匹配地址。无需 session_info 存 check 地址（channel 内部处理）。

### channel 完整封装（listen/connect 内置 DB 地址交换）

channel 进一步包装，业务代码完全不接触 DB 协调细节。listen/connect 内部直接集成 `write_object`(temp)/`wait_obj`，地址交换/端口发现/等待对端就绪全部内置。**db 作为依赖传入**（channel 持有 db 引用）。

```python
# check 侧（监听方）
chan = agent.listen(db, "__rasg__check_channel")
    # 内部自动：① 绑定业务端口（OS 动态分配）
    #          ② db.write_object("__rasg__check_channel", {host, rpc_port}, save_to_db=False)
    #          ③ 返回 PeerChannel（含 accept_n 能力）
chans = chan.accept_n(nsd)   # 接受 nsd 个 compute 连接，返回 {sd: PeerChannel}

# compute 侧（连接方）
chan = agent.connect(db, "__rasg__check_channel")
    # 内部自动：① wait_obj 等 check 写入地址（_wait_for_objects 复用）
    #          ② 读地址 {host, rpc_port}
    #          ③ connect_peer 建立长连接（带重试）
    #          ④ 返回 PeerChannel
resp = chan.rpc(req, timeout=30)
```

**业务只感知**：一个 DB 对象名（`"__rasg__check_channel"`）+ listen/connect/rpc。host/port/temp/wait_obj 全部隐藏。

## 三、失败传播（主动通知优先 + 被动检测兜底）

**核心原则**：channel 的退出**不依赖超时/断连的连锁失败**。检测到不可恢复错误的节点，**主动发送错误信息给其他节点**，让它们立即失败退出。超时/断连只是"检测失败"的手段，检测到后必须主动通知。

### 三层失败检测（按响应速度）

| 层 | 信号 | 触发条件 | 响应速度 |
|----|------|---------|---------|
| 1 | **主动通知** | 某方检测到不可恢复错误，退出前 fan-out 通知所有连接的对端 | 立即 |
| 2 | **断连** | 对端进程死/网络断，TCP RST | 秒级 |
| 3 | **超时** | 对端无响应（兜底） | timeout（默认 30s） |

### 主动失败通知的核心场景（check 作为协调者 fan-out）

check 持有所有 compute 的 channel，有责任在检测到任一失败后**主动通知其余 compute**：

```
compute A 崩溃:
  → check 的 chan[A].rpc_wait() 超时/断连，重连 2 次失败（检测到 A 不可恢复）
  → check 不只是自己退出，而是主动 fan-out：
      for sd in 其余存活的 compute:
          chans[sd].notify_failure("compute A failed, abort solve")
  → 其余 compute 的 rpc 立即收到 error 响应，立即退出（不等超时）
  → check 自己也退出
  → master 收到所有 task TaskFailed

check 自己粗校正异常:
  → catch 异常
  → for sd in 所有 compute: chans[sd].notify_failure("coarse correction failed")
  → 各 compute 立即退出

compute solve 异常:
  → 该 compute 主动 chan.notify_failure("solve error") 通知 check
  → check 收到后 fan-out 通知其余 compute（同 compute 崩溃场景）
```

### check 崩溃场景（被动检测，无 fan-out）

check 崩溃时无法主动通知 → 各 compute 靠断连/超时检测自行退出。这是不对称的（check 是协调者，有 fan-out 责任；compute 是叶子，检测到 check 失败自行退出即可）。但 check 崩溃相对少见（check 只做 IO + scipy，计算轻量）。

### 逐场景验证（含主动通知）

| 失败场景 | 检测方 | 传播方式 | 其余节点退出速度 |
|---------|--------|---------|----------------|
| compute A 崩溃 | check（断连/超时） | check **主动 fan-out** notify 其余 compute | **立即**（不等超时） |
| check 崩溃 | 各 compute（断连） | 无 fan-out，各自检测退出 | 秒级（断连） |
| compute A solve 异常 | A→check（notify）→check fan-out 其余 | 主动通知链 | **立即** |
| check 粗校正异常 | check 自身 | check **主动 fan-out** 所有 compute | **立即** |
| 网络瞬断 | 断连 | 2 次重连恢复 | 恢复继续 |
| 永久断连 | 重连失败 | 主动 fan-out（check 侧）/ 断连退出（compute 侧） | 立即/秒级 |

### notify_failure 的实现

复用 rpc 响应通道（不新增消息类型）：
- `chan.notify_failure(reason)` 内部发一条特殊的 rpc，response 的 status=error + payload=reason
- 对端的 `chan.rpc()` 收到 status=error 立即抛 `PeerFailed(reason)` 异常
- 若对端不在 rpc 等待中（如 check 在做粗校正），failure 入 channel 的 queue，下次 rpc/recv 时取出

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
