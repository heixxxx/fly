# 010 — stop() drain 期 worker 断连误报「worker dead + 副本全灭」ERROR

> 状态：**OPEN**（2026-09-03 立项）
> 来源：DOC_CHANGELOG 2026-08-30 `launch_ssh_workers` QA 验证记录（原文「待立 issue」）。
> 关联：`src/agent/cpp/master_agent.cpp` `on_disconnect` / `handle_worker_death` / `fail_orphan_data_objects`。

## 现象

显式 `master.stop()` 的 drain 阶段，worker 主动断连被误报为
`worker dead + 副本全灭` ERROR（走 `handle_worker_death` →
`fail_orphan_data_objects` 判死链）。`launch_local_workers` 同脚本可复现
相同输出——与 ssh 启动方式无关，属 **stop 时序噪声**：数据已随 WBQ drain
落盘，正常退出被报为数据事故。

## 影响

- **QA/运维信号污染**：正常退出路径产生 ERROR 级日志，真实判死信号被稀释，
  错误告警疲劳；
- 运行摘要/监控的错误计数口径被正常 stop 干扰。

## 既有机制与残余窗口

`on_disconnect` 已有显式三路分派（6b9ff95，2026-08-28，master_agent.cpp
:2091 起）：

1. `shutdown_pending_workers_`（master 主动关停指令先行）/
   `exit_confirmed_workers_`（WORKER_EXIT graceful 声明）→
   `handle_worker_exit`，不进判死链；
2. drain 期未标记断连 / `worker_reconnect_timeout=0` 逃生口 →
   `handle_worker_death`（含 fail_orphan_data_objects）；
3. 其余正常运行期断连 → 宽限等重连。

2026-08-30 的复现发生在该机制在位**之后** → 残余竞态窗口候选：

- worker 完成任务后自行退出（或收到 SHUTDOWN 前退出），断连先于
  SHUTDOWN 投递/标记消费到达 master；
- 标记在 master 发送侧登记，投递是异步的——「已决策未送达」窗口内
  断连落进分支 ②。

## 修复方向（待复测确认后展开）

1. 当前 HEAD 复测（`launch_local_workers` + 显式 `stop()`），确认是否仍复现；
2. 若复现：stop() 入口先对全部存活 worker 打 `shutdown_pending` 标记再进入
   drain（关停上下文内断连默认按 graceful 语义），或 drain 期断连一律走
   `handle_worker_exit`；
3. 回归：确定性 QA case——stop 期断连零 ERROR 断言。

## 关联

- worker 断连重连最终语义（两阶段宽限，`worker_reconnect_timeout=120` 对等）
- 关闭语义 v2：stop() = 等全部 task 完成（drain 核心语义）
