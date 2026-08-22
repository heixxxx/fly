# Fly 运行时 Summary 统计增强 — 设计与实施计划

> 制定:2026-08-22。经四轮用户 review 修订后定稿(修订记录见 §六/§七)。
> 状态:✅ 已实施(全量单测 63 + QA 149 + e2e 验证通过)

## 一、需求(用户提出)

1. **运行时长**:所有任务完成退出时报告总运行时长。
2. **分阶段集群内存**:将总运行时间分 10 等份,每阶段报告集群物理内存
   (master + 全部活跃 worker 的 RSS 之和)的 **total_avg / total_peak**。
   只报集群总量,不做 per-process 明细(用户裁定)。
3. **按 db 统计**:每个 db 报告磁盘用量、创建时长(first_seen → freeze/退出)、
   创建期间集群内存的 total_avg / total_peak。
4. **定时机器信息日志**:每个 worker 每 10s 打一次 INFO 级机器信息:
   本进程内存、host free/total 内存、本进程 CPU 负载、host 综合负载
   (loadavg)。master 同样打(设计推荐,用户未反对)。

所有内存口径均为**物理内存**(RSS / MemFree / MemTotal),不用 VmPeak/VmSize
(2026-08-16 已裁定:虚拟预留与物理压力无关,push OOM 排查中被误读过)。

## 二、调研结论(现状)

| 设施 | 现状 | 差距 |
|---|---|---|
| `src/core/cpp/system_info.cpp` | 有 `meminfo_bytes` / `process_memory`(VmRSS+VmHWM)等,但全是 static 私有,仅服务启动一次性打印 | 需公共数值 API;缺 loadavg |
| `src/main/cpp/main.cpp` `resource_monitor_loop` | 每 5s 打 DBG 级,仅 cpu%+rss,master/worker 共用 | 间隔/级别/字段均不满足需求 4 |
| `HeartbeatMessage`(`message_types.h:116`) | worker→master 每 10s(硬编码,`worker_agent.cpp` heartbeat_loop),on_heartbeat 更新 WorkerManager 后回 ack | 无内存字段——是现成的上报通道 |
| 退出路径 | master 脚本完成 → `agent.stop()`(drain)→ 直接退出;无任何 summary。stop_impl 在 main.cpp `Logger::shutdown` 之前执行,打印点安全 | 无 summary |
| db 生命周期 | 首见锚点 `register_database`(master_agent.cpp:2153);freeze 锚点 = `frozen_dbs_.insert` 成功两处(on_database_freeze/on_master_freeze);merge 走 `set_paths` 原地更新不动 key | 无时间戳记录 |
| 死键发现 | config `heartbeat_interval=5` 仅测试引用,实际心跳硬编码 10s | 与本设计无冲突,不动 |

## 三、方案设计

### 3.1 架构

```
worker 进程                                    master 进程
┌─────────────────────────────┐    ┌──────────────────────────────────────┐
│ resource_monitor_loop(改造)  │    │ 常驻线程样板(heartbeat_check 等)      │
│  每10s INFO 机器信息          │    │ ┌──────────────────────────────────┐ │
│                             │    │ │ RunMetricsCollector(新)           │ │
│ heartbeat_loop(10s 硬编码)   │    │ │  · tick 线程:每10s 快照           │ │
│  hb.rss_bytes_ = VmRSS ─────┼───▶│ │    total = master RSS             │ │
│                             │    │ │         + Σ活跃worker最新RSS      │ │
└─────────────────────────────┘    │ │  · 钩子:register/freeze/merge      │ │
                                   │ │  · freeze 时 du 统计 disk 终值     │ │
                                   │ │  · 退出时:summary 生成与打印       │ │
                                   │ └──────────────────────────────────┘ │
                                   └──────────────────────────────────────┘
```

**核心口径——master 统一快照 tick**:master 每 10s 记一个快照点
`{t, total}`,total = master RSS + Σ活跃 worker 的最新上报 RSS(活跃判定
WorkerManager status ≠ DEAD,判死剔除/复活重计)。所有聚合基于这条统一
时间轴。worker 心跳相位与 tick 不对齐最多差一个周期,对分钟级阶段统计
无影响,换来 avg/peak 语义确定可解释。

### 3.2 system_info 数值 API(基础层)

新增公共 static(现有格式化函数改为调用它们,消除重复):
- `uint64_t process_rss_bytes()` / `process_hwm_bytes()` — /proc/self/status
- `HostMem host_mem_bytes()` — {total, free, available},/proc/meminfo
- `double host_loadavg_1m()` — /proc/loadavg

### 3.3 需求 4:机器信息日志

`resource_monitor_loop` 改造:间隔读 config `machine_info_interval_seconds`
(默认 10,0=关闭);DBG→INFO;字段补齐 host free/total、loadavg:
`MachineInfo proc_rss=512MB (peak 640MB) host_mem=2.1/30.9/62.7GB cpu=85.2% load1=1.25`

### 3.4 需求 2 通道:心跳捎带 RSS

`HeartbeatMessage` 加 `uint64_t rss_bytes_ = 0`,worker heartbeat_loop 发送前
填本进程 VmRSS。不新增消息类型(零新线程、周期天然 10s 对齐、断连重连覆盖、
master/worker 同 binary 无 wire 兼容问题)。master `on_heartbeat` 写入
collector(set-latest,不在 reactor 热路径聚合)。

### 3.5 RunMetricsCollector(新 `src/agent/cpp/run_metrics.h/cpp`)

**采集**(内部一把 mutex):
1. tick 线程(样照 heartbeat_check_loop 生命周期写法,含 running_ 兜底):
   每 `metrics_tick_seconds`(默认 10)记快照;启动时先 tick 一次(短 run 有样本)。
2. db 登记:`record_db_created(db_path)` 挂 register_database;
   `record_db_frozen(db_path)` 挂 frozen_dbs_.insert 成功两处,同时 du 统计
   disk 终值(锁外执行,ms 级,freeze 本身是重 IO 不构成新瓶颈);
   `record_db_paths_changed(db_path)` 挂 set_paths(merge):作废已统计 disk 值
   (frozen_dbs_ 防重 freeze,该 db 归入退出补测)。
3. worker 样本:`on_worker_sample(worker_id, rss)` 挂 on_heartbeat;
   tick 时重算 total。
4. 内存保护:tick 上限 200,000 条(10s ≈ 23 天),超限停采 + WARN 一次。

**汇总**(退出时一次算):
- 分阶段:总时长切 10 等份,逐桶对 total 求 avg/peak;总 tick < 10 时
  退化为单阶段并注明。
- db 窗口:[first_seen, frozen_at 或退出时刻],窗口内 tick 求 avg/peak;
  disk:freeze 时统计值(多数)+ 退出补测值(仅未 freeze/被作废的 db,
  `du -sk db_path` + data_path 非空且不同时补一次求和,popen 捕获 stdout,
  失败报 n/a 不阻断)。典型 run 全部 db 已 freeze,退出路径零 du 零延迟。
- 输出仅集群总量口径(无 per-process 明细,用户裁定)。

**打印点**:`MasterAgent::stop_impl` 尾部(drain 完成、线程 join 后),
fast_exit 同样打(有多少打多少)。此时 Logger 必然存活。

### 3.6 输出格式

```
========== Fly Run Summary ==========
duration: 1234.5s  (dbs: 2, workers seen: 4)
cluster memory (physical RSS, master+workers) by phase (10 phases):
  phase 01 [   0.0s,  123.4s]: total_avg= 480MB  total_peak= 512MB
  phase 02 [ 123.4s,  246.9s]: total_avg= 605MB  total_peak= 640MB
  ...
per-database:
  db=run/A: disk=120MB  duration=300.2s  frozen           mem total_avg=480MB  total_peak=520MB
  db=run/B: disk= 80MB  duration=900.0s  active-at-exit  mem total_avg=600MB  total_peak=640MB
```

### 3.7 配置键(全部有默认,零配置可用)

| 键 | 默认 | 用途 |
|---|---|---|
| `machine_info_interval_seconds` | 10 | 需求 4 日志间隔(0=关) |
| `metrics_tick_seconds` | 10 | master 快照间隔 |

## 四、实现清单(TDD 顺序)

1. system_info 数值 API + 单测(/proc 实读对照,容差断言)
2. resource_monitor_loop 改造 + config 键
3. HeartbeatMessage.rss_bytes_ + worker 填充 + master 接线
   (agent_network_test 断言 master 收到 rss>0)
4. RunMetricsCollector + 单测(模拟 tick/心跳/db/freeze/merge/判死,
   验证分桶 avg/peak、窗口、作废重测、上限、短 run 退化)
5. master 挂钩 + summary 格式单测
6. BUILD 接线、全量单测、QA 回归 + 定向 case(短 run、无 db、多 db)

## 五、边界与风险

- 心跳 10s 硬编码:统计只依赖"最新值",心跳间隔变化无正确性风险。
- stop() 时 worker 已退出:summary 全基于 master 本地历史数据,不受影响。
- du 失败兜底:freeze 时失败留空 WARN,退出重试一次,仍失败报 n/a。
- 统计口径声明:avg/peak 为"每 10s 采样瞬时值的均值/最大",非精确积分,
  summary 表头注明采样间隔。

## 六、用户 review 修订记录

1. 第一轮:summary 只报全集群 total_avg/total_peak,去掉 per-process 明细;
   磁盘用 du -sh 复用而非文件遍历。
2. 第二轮:db disk 在 freeze 结束后立即统计(终值确定),退出时仅对未完成
   db 补统计,避免退出延迟。
3. 其余决策点按推荐执行:master+worker 都打机器信息、仅日志无 Python API、
   阶段数固定 10。

## 七、增强 v2:心跳失败样本不丢 + 成组合并(2026-08-22 用户指令)

**问题**:worker 心跳 `try_send` 失败(reactor 背压/断连重连窗口)时,
捎带的 RSS 采样被丢弃("Heartbeat skipped"),集群内存序列出现缺口。

**方案**(用户三轮裁定):
- **worker 侧**:样本缓冲不丢——发送失败的样本留在 pending 队列,下次
  心跳连同新样本成组发送;缓冲上限 3600 条(10s 间隔 ≈ 10 小时,超限
  丢最旧 + WARN)。
- **样本时间戳(用户裁定)**:每样本携带**真实时间点**——unix epoch
  毫秒(system_clock 采样时刻),**不用相对时间戳/间隔反推**(完全不准
  被否决)。消息为平行数组:`rss_epoch_ms_[]` + `rss_bytes_arr_[]`。
- **master 侧时间精度(用户裁定)**:master 自身 tick 为骨架,骨架同时
  记 steady 相对时刻(渲染时间轴,保单调)与 epoch 毫秒(对齐域);
  worker 样本按 epoch **吸附(snap)到最近的 tick 槽位**。
- **合成(退出时)**:对每个骨架 tick:total = master RSS(该 tick 实测)
  + Σ各 worker 在该 tick epoch 时刻的最新已知值(≤ 该时刻最近样本,
  阶梯展开;首样本前不计 = 未上线;判死时刻 dead_epoch 之后不计,
  复活后新样本(epoch > dead_epoch)自然重新生效)。
- **判死钩子**:master 判死 worker 时通知 collector 记 dead_epoch
  (替代原 alive_fn——合成已推迟,判死改为区间截断,复活无需显式钩子)。

**渲染层不变**:render_summary 仍消费"合成后的集群 total 序列",
SummaryInput 接口与全部纯函数测试保持原样。

## 八、增强 v3:输出形态改为 summary 文件(2026-08-22 用户第四轮裁定)

- **summary 内容不进日志**:分别直写 `{log_dir}/runtime.summary`(时长 +
  分阶段集群内存)与 `{log_dir}/db.summary`(按 db 统计)。
- **独立 ofstream 直写**(构造即开、析构 flush+close),不经 Logger 通道
  ——e2e 实测退出期 Logger INFO 有吞行现象([SD] stderr 取证:summary 生成
  正常、INFO 全部未落 master.log;与 2026-08-19 压测已知现象一致),文件
  通道绕开该问题。
- **master 用户日志只打一行**(message 系统,新编号 `FLY::0002`,需在
  MessageRegistry 注册否则 MSG 静默 no-op):总耗时 + 两文件地址。
- master.log 另留一行 INFO 索引(尽力;被吞不影响文件)。
- db 创建入口有两个(register_database + get_or_create_database——后者是
  Python open_db/master 自写路径),首见锚点两处都挂。
- 小 db 磁盘显示自适应(KB/MB/GB),避免 du -sk 的 4KB 被取整成 0MB。
