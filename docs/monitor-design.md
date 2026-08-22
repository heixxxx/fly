# Cluster Monitor 设计文档

> 模块：`src/monitor`（C++ 采集落盘层 + Python Web GUI）
> 关联：`docs/run-summary-metrics-design.md`（RunSummary，喂样通道已迁移至 monitor）

## 1. 概述

cluster monitor 提供 fly 集群运行状态的实时观测与事后回放：

- **实时**：run 进行中打开 GUI（只读连接，不打扰 master 单写），3s 轮询刷新。
- **事后**：run 结束 `monitor.db` 为干净终态，可随时独立打开浏览全量历史。

三层数据流（与心跳完全解耦，心跳回归纯保活单一职责）：

```
worker: monitor_report_loop 线程（1s 采样/10s 成组 MONITOR_SAMPLE 上报，
        失败与断连缓冲成组补发）
  + TaskResourceTracker（task 执行窗口 RSS/CPU 归属，随 TASK_COMPLETE/FAILED）
  + Python task_io（read_object/write_object 计时，聚合随 COMPLETE，明细走
    MONITOR_TASK_IO 尽力而为通道）
master: on_monitor_sample（喂 RunMetrics + 落库）/ 事件点全景（task/worker/
        db/run/sched/storage 五类事件）→ MetricsDb 单写（队列+写线程批量事务）
        → {log_dir}/monitor.db
GUI:   serve.py（stdlib）只读 JSON API + ECharts 前端（vendor 进库，无构建链）
```

## 2. SQL 选型调研结论

| 候选 | 结论 | 依据 |
|---|---|---|
| **SQLite（rollback journal）** | **选用** | 单写多读是其支持模型；Python stdlib + C++ vendor 零依赖；单文件嵌入式 |
| DuckDB | 排除 | 一个进程持写锁时其他进程连只读都打不开（官方文档）——实时监控不可行 |
| LMDB | 排除 | mmap 跨 NFS 客户端无一致性保证；非 SQL |
| RocksDB/LevelDB | 排除 | 官方明示不支持 NFS（mmap） |
| PostgreSQL/MySQL | 排除 | C/S 架构违背嵌入式单文件直读，运维负担 |

**NFS 约束**（[SQLite WAL 文档](https://www.sqlite.org/wal.html)）：
- **禁止 WAL 模式**（跨进程 mmap 共享内存在 NFS 上不可用）——MetricsDb 固化
  `journal_mode=PERSIST`（兼避免每事务建删 journal 文件的 NFS 元数据抖动）。
- `synchronous=NORMAL` + 写线程批量事务（千级提交风暴合并单事务）+ close 时
  清干净 journal（终态文件事后只读 100% 安全）。
- 单写者（master）架构下 NFS 锁丢失的最坏后果是 GUI 读到 BUSY（重试），
  无第二写者不会损坏。

## 3. 采集口径

### 3.1 worker_samples（事件驱动为主 + 周期兜底）

**采样模型（用户裁定原则）**：task start/end、IO 读写、assign/断连/注册等
cluster 事件的发生时刻是天然采样点——事件密集期样本自然加密、空闲期稀疏、
突发负载窗口不依赖固定间隔撞运气；固定间隔只是兜底节奏。

| 机制 | 说明 |
|---|---|
| 事件采样（kind=1） | assign 到达 / 执行起止 / internal task 起止 / 断连 / 注册完成（worker 侧 `sample_now_event`）；task 完成 / worker 注册 / 收到样本（master 侧 `monitor_self_event`）。任意线程可调（MonitorSampler 内部互斥） |
| IO 事件采样 | read 结束点（数据必在内存；耗时 ≥5ms 或 ≥256KB 才采——快 read=cache 命中=无新内存）；write 时刻**无条件采**（write 快 ≠ 进程内存小，用户其它大对象与所写对象无关）。采得的峰值补入 task 窗口（TaskResourceTracker） |
| 执行期加密 | 有 task 在跑时 monitor 线程周期降至 `monitor_exec_sample_interval_ms`（200ms） |
| 周期采样（kind=0） | 空闲时 1s 一条兜底 |
| 统一节流 | 全部样本共用最小间距（200ms）——事件风暴/快 IO 不刷爆 DB |

| 字段 | 口径 |
|---|---|
| proc_cpu_bps | 进程 jiffies 增量 / 机器总 jiffies 增量（10000=吃满全部核），整数定点 ×100 |
| host_cpu_bps | 机器非 idle 占比 |
| net_read/write_bytes | 本进程 TCP 累计字节（NetStats，插桩 TCPSocketTransport 四方法=全部流量咽喉）；单调，消费侧差分成速率 |
| proc_rss / host_mem | /proc 物理内存口径 |
| kind | 0=周期 / 1=事件（GUI 曲线两种样本同轴混排） |

master 自身：自监控线程（10s 周期）+ 事件采样直写 worker_id=0 行。

### 3.2 tasks 行（执行窗口指标，worker 上报）

| 字段 | 口径 |
|---|---|
| created/ready/started/completed_ms | master 侧调度链（submit/依赖满足/assign/终态） |
| exec_start/end_ms | worker 真实执行窗口（区别派发时刻） |
| cpu_time_ms | getrusage 微秒差分（utime+stime 全线程和）——亚秒 task 亦有效 |
| read_time/write_time_ms | read_object 耗时 / write_object+drain 落盘耗时 |
| read_bytes / write_bytes | 解压后 / 压缩后字节（write 以 C++ WriteRecord 为准） |
| mem_baseline/avg/peak | 窗口内进程 RSS（绝对口径；delta=减 baseline） |
| dbs | 提交时解析 `__fly_db__:` 编码 args + inputs/outputs 对象名前缀 |

**内存峰值的观测点**（不依赖固定采样间隔）：begin/end 端点采样 +
write 时刻（待写对象及用户持有的其它对象必存活）+ read 结束（数据刚入内存）
+ 执行期 200ms 加密周期。IO 的单次带宽由 object_io 的 bytes+duration 直接
可算（read 开始/写结束点不重复采样——无内存信息量）。

**CPU%/IO 密集判别**：exec 时长、cpu_time、read_time+write_time 四元对比
（GUI Tasks 页占比条自动计算）。

### 3.3 events（统一事件流，append-only）

- task：SUBMIT / ASSIGN / COMPLETE / FAIL / REQUEUE / STALE_REPORT_DROPPED /
  PERSIST_FAILED / RESTART
- worker：REGISTER / DISCONNECT / DEAD / REVIVED / PROPERTY_UPDATE
- db：DB_CREATED / DB_FROZEN / DB_MERGE_START / DB_MERGE_DONE /
  DB_MERGE_FAILED / DB_PATHS_CHANGED
- run：DRAIN_START / DRAIN_DONE / FAST_EXIT
- sched/storage（P1 异常）：SCHED_STALLED（仅首次翻转）/ WRITE_REJECTED×3 /
  AUTO_BACKUP_TRIGGER / STORAGE_TAKEOVER / ORPHAN_FAIL

挂点原则：**只挂状态实际变化处**（如 check_and_move_to_ready 的 ready 点、
schedule 成功处），绝不挂 200ms/3s 周期循环入口。

后续扩展（P2，当前不做）：ATTR_DEGRADED（需 TaskScheduler 加回调）、
ObjectCache 淘汰计数、message count 对账明细。

### 3.4 object_io（对象级明细）

worker 侧 read_object/write_object 单次调用（名称/方向/字节/耗时/epoch），
经 MONITOR_TASK_IO 尽力而为上报（发送失败丢弃——聚合四元组随
TASK_COMPLETE/TASK_FAILED 有不丢保障）。

## 4. Schema

见 `src/monitor/cpp/metrics_db.cpp` 的 `kSchemaSql`（唯一权威定义）：
`meta` / `workers` / `worker_samples`（PK worker_id+epoch_ms 幂等）/ `tasks`
（UPSERT 快照）/ `object_io` / `events`。终态 task 在 master 内存仅 100 条 LRU，
DB 事件时即写保全量历史。

## 5. GUI 使用

```bash
# 方式一：任意 python3（≥3.8，stdlib-only，独立于 fly）
python3 src/monitor/py/serve.py <monitor.db|log_dir> [--port 8788]

# 方式二：fly 二进制（推荐，无 PATH 依赖）
fly --serve-monitor <monitor.db|log_dir> [--port 8788]

# 方式三：fly 脚本内一行启动（detached，打印 URL）
import fly; fly.launch_monitor_gui("<log_dir>")
```

启动后打印各网卡入口 URL。五个页面：
- **总览**：run 时长 / task 计数 / 集群聚合 RSS、CPU 曲线 / 最近事件流。
- **Workers**：worker 卡片（最新 CPU/内存/角色/状态）；详情——进程 vs 机器
  CPU%、RSS vs host 可用内存、网络读写速率、load1、该 worker 全部 task。
- **Tasks**：搜索/状态/worker 过滤分页；执行时长/CPU time/**CPU-IO 占比条**/
  读写时间字节/内存 avg-peak/关联 db；详情——调度链四时间戳、事件流、
  对象 IO 明细。
- **Timeline**：按 worker 分泳道的 task 执行窗口 Gantt（缩放/平移）。
- **DBs**：db 生命周期事件时间线 + tasks.dbs 反查关联。

## 6. 配置

| key | 默认 | 说明 |
|---|---|---|
| monitor_db_enabled | 1 | master 单写 monitor.db（0=本 run 无持久化） |
| monitor_sample_interval_ms | 1000 | 周期采样间隔（空闲兜底节奏；0=关闭上报） |
| monitor_report_interval_ms | 10000 | worker 成组上报/master 自监控周期直写间隔 |
| monitor_exec_sample_interval_ms | 200 | 最小采样间距：执行期加密档 + 事件采样统一节流下限 |

## 7. 代码地图

```
src/monitor/cpp/
  monitor_types.h        TaskRow/ObjectIoRecord/TaskResourceAgg 共享结构
  monitor_sampler.{h,cpp} 差分采样器（CPU jiffies/内存/net/loadavg）
  task_resource_tracker.*  task 窗口资源归属（begin/end/add_sample/take_agg）
  metrics_db.{h,cpp}     SQLite 单写器（队列+写线程+批量事务+PERSIST journal）
src/monitor/py/
  task_io.py             Python IO 计时归属（database.py/executor.py 插桩）
  serve.py               Web GUI（stdlib）+ launch_monitor_gui
  static/                前端工程（ECharts vendor + ES modules 五页面）
third_party/sqlite/      SQLite 3.46.1 amalgamation（公有领域 vendor）
qa/monitor/              test_monitor_db / test_monitor_gui
```

关键集成点：`WorkerAgent::monitor_report_loop` / `send_master_or_buffer`（资源
字段统一填充咽喉）/ `MasterAgent::on_monitor_sample` / `on_task_io_report` /
`record_task_snapshot`（task 事件点）/ `monitor_self_loop`。
