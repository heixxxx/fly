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
- 单写者（master）架构下 NFS 锁丢失的最坏后果是 GUI 读到 BUSY（短重试，
  用尽转 HTTP 503——前端静默跳过本轮，下一轮轮询补上），无第二写者不会
  损坏。

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
| dbs | 提交时解析 `__fly_db2__:` 编码 args + inputs/outputs 对象名前缀 |

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

启动后打印各网卡入口 URL（WSL/NAT 环境含真实出口 IP，Windows 宿主浏览器可用）。
GUI 数据跟随：monitor.db 随 run 轮转目录隔离（fly_log.N）；`--serve-monitor
fly_log.latest` 始终指向最新 run 且新 run 落盘后自动跟随（软链 + inode 检测）。
刷新模型：数据指纹（库 inode / task 计数 / worker 数 / 最新样本时刻）不变
则跳过重渲染（图表缩放/hover/滚动零打断）；库被整体替换（inode 变化）或
run 切换时自动清前端增量缓存重建；样本经批量增量通道拉取（`/api/samples`，
逐 worker 游标——总览页全部 worker 每轮一次请求）；run 结束且数据稳定后
自动停轮询。

五个页面：
- **总览**：run 时长 / task 计数 / 集群聚合 RSS、CPU、网络速率曲线 / 磁盘 IO
  占位 / 最近事件流。聚合按 **1s 时间桶**对齐（各 worker 样本 epoch 是独立
  毫秒时间戳，按精确时刻分组几乎永不重合——旧实现"Σ"曲线每个点只含单个
  worker，实测 ΣRSS 峰值=单 worker 峰值；桶内每 worker 取最新样本，单机
  Σ=全部 fly 进程合计可与机器 CPU 直接对比，多机进程 Σ 可 >100% 图例注明）。
- **Workers**：worker 卡片（最新 CPU/内存/角色/状态）；详情——进程 vs 机器
  CPU%、RSS vs host 可用内存、网络读写速率、load1、该 worker 全部 task。
- **Tasks**：搜索/状态/worker 过滤分页；执行时长/CPU time/**CPU-IO 占比条**/
  读写时间字节/内存 avg-peak/关联 db；详情——调度链四时间戳、事件流、
  对象 IO 明细。超长名称（task/对象/db 路径）默认首尾缩略 + `....`，点击
  展开为折行全名（不破坏布局）。
- **Timeline**：按 worker 分泳道的 task 执行窗口 Gantt（滚轮缩放/滑块拖选，
  实时显示当前视图范围；「复原缩放」回全程；泳道含 host 标签，支持按
  worker id / 按 host 排序；点击条形弹出驻留详情面板）。
- **DBs**：简化视图——db 列表 + 创建时间 + 冻结时间 + 磁盘占用(GB)（freeze
  终值 / run 结束补测，经 DB_DU 事件落库）。

单位口径（用户裁定）：**内存与磁盘占用统一 GB、三位有效数字**（fmtGB）；
网络速率与 IO 字节量保持自适应单位。

### 5.1 多语言与主题

- **语言**：header 右侧下拉切换 **中文（默认）/ English**，持久化
  localStorage（`fly-monitor-lang`），刷新后保持。字典集中在
  `js/i18n.js`（ZH/EN 两份 key 完整一致，冒烟测试断言防漏译；`t(key,
  ...args)` 缺 key 回退中文、再缺显示 key 本身——开发期肉眼可见）。
  语言切换 = 整页重建（复用 mount/update 模型）：mount 模板文案取新语言，
  当前页上下文（worker/task 详情、过滤条件、timeline 缩放/排序）全部保留。
- **主题**：header 右侧下拉切换 **浅色 / 深色 / 跟随系统（默认）**，持久化
  localStorage（`fly-monitor-theme`）。实际主题落在 `<html
  data-theme="light|dark">`，CSS 变量双套值（app.css `--bg/--panel/--text/
  --ser-*` 等，两套集合一致性由冒烟测试断言）；`index.html` 头部内联脚本
  在 CSS 渲染前先落 data-theme（防首帧闪错色）。跟随系统经
  `matchMedia(prefers-color-scheme)` 解析，系统切换实时生效。
- **ECharts 主题联动**：canvas 不消费 CSS 变量——图表色（轴线/标签/
  tooltip/数据系列色/timeline 负载维度色）经 `theme.cssVar()` 实时读取；
  主题切换随整页重建新建图表实例，自然取到新值。
- 曲线名（Total Proc RSS / Host CPU 等）与状态枚举（COMPLETED/EXITED）
  保持英文原文——专有指标名不翻译（用户裁定口径：图表曲线名不用数学
  符号、用英文全称）。

## 6. 当前未监控项（待后续增强）

- **进程磁盘 IO 速率**：总览页为占位图（"暂未支持"）。候选方案：
  `/proc/self/io` 的 read_bytes/write_bytes（实际块设备 IO，进程级、读取
  成本与 RSS 采样同量级）——可作为 MonitorSample 新字段随现有通道上报，
  事件驱动采样点（write 前后）同样适用。
- ATTR_DEGRADED 事件（TaskScheduler 需加回调）、ObjectCache 淘汰计数、
  message count 对账明细。

## 7. 常见问题（用户脚本侧）

- `@as_task(inputs=...)` 的 lambda **必须与 task 函数同签名**（收到全部
  参数）：`inputs=lambda db, keys, out_key: [...]`，写 `lambda db, keys:`
  会报 "takes 2 positional arguments but 3 were given"。
- `wait_tasks()` 在任一 task 失败时**抛 RuntimeError 中断脚本**（失败显式
  语义）；脚本想容忍失败需 try 包裹。
- `write_data(db, key, N)` 写入的是对象 `N` 本身（pickle 序列化）——传
  `3*1024*1024` 只写几十字节的 int；要写大对象需传 `os.urandom(N)` 或
  实际数据。

## 8. 配置

| key | 默认 | 说明 |
|---|---|---|
| monitor_db_enabled | 1 | master 单写 monitor.db（0=本 run 无持久化） |
| monitor_sample_interval_ms | 1000 | 周期采样间隔（空闲兜底节奏；0=关闭上报） |
| monitor_report_interval_ms | 10000 | worker 成组上报/master 自监控周期直写间隔 |
| monitor_exec_sample_interval_ms | 200 | 最小采样间距：执行期加密档 + 事件采样统一节流下限 |

## 9. 代码地图

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
    js/i18n.js           中英双语文典 + t()（localStorage 持久化，默认中文）
    js/theme.js          浅色/深色/跟随系统（data-theme + cssVar 供 ECharts）
    js/storage.js        localStorage 安全封装（隐私模式/禁用站点数据回退内存）
    js/floatbar.js       智能顶部浮窗公共实现（Tasks 筛选栏 / Timeline 工具栏）
third_party/sqlite/      SQLite 3.46.1 amalgamation（公有领域 vendor）
qa/monitor/              test_monitor_db / test_monitor_gui
```

关键集成点：`WorkerAgent::monitor_report_loop` / `send_master_or_buffer`（资源
字段统一填充咽喉）/ `MasterAgent::on_monitor_sample` / `on_task_io_report` /
`record_task_snapshot`（task 事件点）/ `monitor_self_loop`。
