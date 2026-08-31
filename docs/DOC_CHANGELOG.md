# 文档变更记录

---
---

## 2026-08-31 (10): runqa 超时善后误杀修复 + stability 孤儿清理——-j6 并发安全化

- **背景**：`-j6` 稳定性测试首轮 14 连败。排查实锤为**工具链双缺陷**而非
  产品问题：① runqa 超时善后用全局 `pgrep` 向全机 fly 进程发 SIGUSR1
  （handler = dump 后退出）+ 全局 `pkill -9`——同机其他 runqa 的进程被
  连环误杀（无幸 case 目录残留 `.fly.{pid}.stack` 为物证）；② 外层
  stability 脚本被中断时 runqa 孤儿化存活，与后续轮并发互踩（三份
  stability 产物目录的孤儿轮日志与被污染轮逐条时长一致，实锤互写）。
- **修复 A（runqa）**：超时善后全部改为**进程组精确打击**——新增
  `_group_pids(pgid)`（读 /proc/*/stat 的 pgrp 过滤），SIGUSR1、/proc
  诊断、存活核验全部只看本 case 进程组（worker 未 setsid 与 master 同
  组，killpg 完备）；删全局 pkill。
- **修复 B（stability_test.sh）**：INT/TERM trap 与轮超时善后补全
  runqa 进程树清理（此前只清 fly，runqa python 进程会孤儿化）。
- **性能数据**：干净 `-j6` 全量 83-92s/轮（`-j4` 约 112s），10/10 绿；
  `-j4` 回归一轮绿。
- 撤前注：本轮早前"7 个时序敏感缺陷"的立项结论作废——那是孤儿互踩
  污染数据，非产品缺陷（含对 mapreduce "10x 劣化"的误读：62s 是孤儿
  扫射后的卡死形态，干净 `-j6` 下 1.9s）。

## 2026-08-31 (9): §〇-A 竞态修复——NOT_READY 协议错误码 + check 圈级收集

- **协议**：`PeerRpcWireStatus::NOT_READY = 4` 新成员（与 RESPOND_FAILURE
  协议层区分；payload 只带诊断消息，判定走错误码——用户裁定弃用字符串
  约定）。全链：wire 枚举 → `PeerRpcStatus::NOT_READY=4` → response_handler
  映射 switch → `PeerRpcServer::send_not_ready` /
  `WorkerAgent::peer_rpc_respond_not_ready` + 导出 + Python 包装。
- **serve 侧**（ras_graph_dynamic `_serve_loop`）：参数未就绪（compute
  注入竞态窗口）回 NOT_READY（可恢复）；fail_hint 与真实计算异常照旧回
  RESPOND_FAILURE。
- **check 侧**（`check_dyn_task`）：收集循环改圈级——NOT_READY 跳过立刻
  请求下一成员（不单点阻塞），全员贡献集齐才开始计算（拼解/收敛判定/
  粗校正边界不变）；有缺则对缺失成员再一圈（请求预构造同轮复用），
  圈间固定退避 10ms；累计 30s 超时判组死。断连/真失败/poison 照旧立即
  判死（语义不变）。
- **根因背景**：compute 注入与 check 驱动无依赖边（08-27 dynamic 创建起
  即缺），此前被旧主循环持 GIL 阻塞的副作用掩盖（请求处理被推迟到注入
  完成后），执行上提消除压制后显形（稳定性 round 19，窗口 3ms）。
- 验证：两命中场景 ×10（20/20）→ solver 全量 → agent/network 单测 21/21
  → 全量 QA 全绿。

## 2026-08-31 (8): T5 DIGEST wire 根摘要双侧消除

- **serve（data_server.cpp）**：分片流取消 `root.update()` 单遍根摘要累积，
  DIGEST 尾帧 `root_crc_` 发 0（帧保留——client 以 DIGEST 为流结束标记 +
  chunk_count 对账）。
- **client 双侧**：`network_chunk_source`（TIER2 块解析器）与
  `data_client_pool`（整帧快路径）均改 `root_crc != 0` 才验——0 = serve 未
  计算（L0 块 CRC + trailer 已承担完整性），非 0（旧 serve）照验，向后兼容。
- 验证：C++ 单测 73/73 + 全量 QA 全绿。

## 2026-08-31 (7): T4 C++ ObjectCache 对齐——删死 low_ 池，单层化

- **object_cache.h**：删除 low_ 池全集（get_low/put_low/low 统计/low 访问器）
  ——§4.7 读恒走数据源后 low 池零生产消费（仅测试调用的过期 API，用户裁
  定）。单层化保留 high：typed C++ 对象（read_object<T> 命中快路径）。
- **导出**：`ex_stg_cache_stats` 改 4 元组（high_*）；QA 消费脚本同步。
- **测试**：object_cache_test low 族删除，eviction/保护窗/计分语义测试迁
  high 层保留覆盖；DbTest 的 low_size 断言改 high_size（幸存不变量：write/
  压缩读不 populate 任何缓存）；data_corruption_test 的 low 投毒用例删除
  （前提已死，盘/远程损坏路径仍有覆盖）。
- 验证：C++ 单测 73/73 + 全量 QA 全绿。

## 2026-08-31 (6): T3 dynamic coarse 双对象拆分——static 默认 low / ac 显式 none

- **ras_graph_dynamic.py**：`__rasg__coarse_prebuilt` 单对象拆为
  `__rasg__coarse_static`（P 数组/N/Nc/b/stride，只读，消费默认 low）+
  `__rasg__coarse_ac`（Ac 三数组，splu 消费会原地重排，消费方显式
  `cache="none"` 每次全新反序列化）——按缓存三分层规范（§14.12）落地，
  splu 污染防护由"消费拷贝"简化为"分层隔离"。
- 验证：solver 目录 QA 全绿（含 coarse case）。

## 2026-08-31 (5): T2d temp 写流式化——open_write_stream 参数化 temp sink

- **C++**：`Database::open_write_stream(name, py_name, temp=false)` 参数化——
  temp=true 走 `open_temp_write_stream`（temp_writer_ 增量直写，正式路径
  镜像；commit 回调语义对齐 put_temp_data：盘写 → INCOMPLETE → COMPLETE
  （带 entry）→ record_write → register_write；entry hash 留空）。内存
  R+常数，取代旧 write_temp_pickle 的整对象 dumps + 整 record 压缩两份
  全量缓冲（R+2C）。
- **删除**：`write_temp_pickle`（C++ 方法 + `_write_temp_pickle` 导出）。
- **database.py**：`_write_temp` pickle 分支改流式（frozen 显式 raise；
  错误经 WriteErrorType 判定；temp 池预热 size 取 stream.total_uncompressed）。
- 验证：C++ 单测 73/73 + 全量 QA 全绿。

## 2026-08-31 (4): T2c 写侧恒流式——threshold 开关与非流式分支退役 + 测试-only C++ 方法清理

- **database.py**：`write_object` 删 `streaming_write_threshold` 分流与非流式
  分支——`open_write_stream → finish_and_commit` 是唯一写路径（与读侧
  2026-08-30 恒流式裁定对齐，无逃生口）。
- **config.cpp**：删 `streaming_write_threshold` 默认键；qa 两个 streaming
  case 的 set_int 行删除。
- **导出/C++**：`_commit_stream` 导出×3 删；C++ `Database::commit_stream`、
  `write_pickle_bytes`、`compress_pickle_bytes` 删（用户裁定：调用仅存在于
  测试的 API 已过期；`read_object_compressed` 有 ObjectCache populate /
  low-tier 内部消费，保留）。
- **语义对齐**：本地 idx entry 的 `write_context_hash_` 在恒流式下有意留空
  （provenance 权威 = master 侧 register；restore 去重/读侧对空 hash 保守
  加载）——`BareWriteObjectHasNonEmptyContextHash` 断言按新语义更新。
- **测试迁移**：database_test / data_service_test / write_registration_test /
  object_cache_test / worker_agent_test 的造数 helper 统一改
  `open_write_stream → write → finish_and_commit`。
- 验证：C++ 单测 73/73 + 全量 QA 全绿。

## 2026-08-31 (3): T2b 死 API 清理——恒流式后无消费者的直写直读导出退役

- **storage_export.cpp**：删 `_write_pickle_bytes`×3 / `_read_streaming`×2 /
  `_read_decompressed`×2 / `_is_temp` / `_decompress_bytes` /
  `_compress_pickle_bytes` 导出及其共享实现
  （read_decompressed_impl / decompress_into_bytes / trailer_expected_size）。
  C++ 侧 `write_pickle_bytes` / `compress_pickle_bytes` /
  `read_object_compressed` 方法保留（单测与 ObjectCache populate/low-tier
  内部路径仍用）；`_write_temp_pickle` 暂留（T2d temp 流式化时处理）。
- **测试迁移**：storage_test.py / test_database_freeze_protection.py /
  test_distributed_data_flow.py 改走恒流式路径
  （open_write_stream→finish_and_commit 写、ex_stg_open_read_stream 读、
  typed 对象走 `_read_from_db`）；test_read_cache_invalidation.py 直写场景
  删除（生产路径覆盖）。storage_test.py 补 `_drain()`（裸 pytest 进程无
  task 收尾排空，写后立读会 TIER1 盘 miss）。
- **database.py**：write_object / raw 注释区更新（直写直读导出已删）。
- **遗留**：storage_test.py 全文件跑时 test_fly_database_mixed_cpp_python
  因裸进程 DataService 单例跨 open_db 状态累积报 corruption（单测/双测组
  合均过）——非 gate 文件预存在问题，与本轮删除无关，待专门排查。

## 2026-08-31 (2): 执行上提重构（消灭 C++→Python 反调）+ flows 迁移缺陷修复

- **chunked-transfer-design.md §14.13（新增）**：sd9/project 超时破案
  （poll_task_blocking 持 GIL 阻塞 100ms → 每 PeerRpc 固定 +100ms）、
  take_task/finish_task 原语设计、Worker.poll_loop 主循环、导出层变更、
  数值结论（纯 RAS 固有轮数：n50/sd9 110 轮、n20/sd4 48 轮，v1/dynamic
  数学等价性单进程模拟验证）、ras_matrix "no ctx" race 遗留指引。
- **architecture.md（线程表）**：Worker Main Thread 职责更新为
  poll_loop()（take→执行→finish），标注执行上提与"不反调"原则。
- **project-design.md §4.3（solve）**：kickoff 代码示例更新——DB 对象
  入库（替代 npz 中转）、非阻塞 solve_ras_graph_dynamic 提交（替代阻塞
  solve_once + ras_graph_coord）、ensure_workers 补 check 宿主申请、
  `__rasg__sol` 产出点改链尾 _teardown。
- **HANDOFF.md（重写头部）**：本轮完成摘要 + §〇-A ras_matrix 偶发 race
  诊断证据与修复方案（专门会话交接）+ §〇-B 调试基建（get_peer_info）。

## 2026-08-31 (1): monitor GUI 性能与健壮性轮——批量增量端点 + N+1 消除 + 前端公共模块收归

**① serve.py 后端**：
- 新增 `/api/samples` 批量增量端点：`after` 为「worker_id:游标」逗号串，各
  worker 独立游标（样本 epoch 取自各 worker 时钟，全局游标会永久跳过时钟
  落后者），分块 OR 查询防 SQL 变量数上限——总览页全部 worker 每轮一次
  请求，替代逐 worker 一请求（数百 worker 时 200+ HTTP 请求不可接受）。
- `api_workers` / `api_events` 的 N+1 消除：关停指令时刻一条
  `GROUP BY worker_id` 批量取（`_shutdown_cmd_epochs`），worker 最新样本
  `MAX(epoch_ms) JOIN` 批量取——`/api/workers` 是前端每轮轮询热点。
- BUSY 语义显式化：重试用尽由「静默返回空结果」改为抛 `DbBusy` →
  HTTP 503（前端对非 2xx 静默跳过本轮，下一轮轮询补上）；重试期间的连接
  重开移入 `_conn_lock` 内（并发线程绝不能再看到被 close 的旧连接），
  连接构造收归 `_connect_ro` 单点。
- `/api/meta` 新增 `db_gen`（库 inode）进指纹——库被整体替换（测试重建/
  run 重置）且各计数恰好相同时仍强制刷新并清前端增量缓存。
- `api_tasks` / `api_events` 的 limit/offset 钳制（1..1000 / ≥0，客户端
  可控参数无界大不再拖垮轮询）；`_gui_alive` 改探测 `/api/meta` 并校验
  响应形状——记录端口被其它服务复用时不再误判「已有实例」拒绝启动。

**② 前端**：
- 新模块 `js/storage.js`：localStorage 安全封装（隐私模式/禁用站点数据下
  裸访问直接抛 SecurityError 曾致整页白屏），i18n/theme/api 统一经此存取。
- 新模块 `js/floatbar.js`：智能顶部浮窗公共实现（判定单一来源改 main
  scroll 驱动），替代 Tasks 筛选栏 / Timeline 工具栏两处重复实现。
- overview 样本改批量增量通道（`fetchAllSamplesIncremental`）；tasks 终态
  详情跳过重拉重渲（保住展开名称/错误信息等交互状态）；timeline 滑块
  document 级拖动监听随页解绑（泄漏修复）、`lanes.indexOf` 改 Map 查找
  （renderItem 热路径 O(n²)→O(n)）；workers 卡片 attributes/role 一律
  escapeHtml（XSS 加固）；各页 update 在 await 返回后校验页面仍挂载
  （切页竞态防御）；app.js 轮询防重入（`pollBusy`）；浮窗内部可滚容器
  滚轮优先原生消费（透传 main 的例外）。
- 冒烟测试同步：`/api/samples` stub + `db_gen` 字段。

**验证**：前端冒烟测试通过、qa/monitor 2/2（test_monitor_db / test_monitor_gui）。

---

## 2026-08-29 (2): §4.6 统一块模型 + §4.7 low-tier cache 取消——差异讨论 #2/#3/#4 定案

**§4.6 统一块模型**：实现与用户构想逐条对照后收敛——正常传输路径双方零块感知、块结构知识只在 client 校验侧与 trailer 元信息；三项待实施（B' trailer 块位置表 / A' 接收线程块级 CRC + resend byte-offset + 去帧级 CRC / C 写路径 WBQ 后台落盘）；META 字段裁定（trailer_len/comp_type 保留、frame_bytes 退役、保持 DATA_RESPONSE 复用）。§12 块索引条款废止。

**§4.7 low-tier cache 全量取消（用户裁定）**：远程读统一走磁盘（实测盘 IO 藏在网络 IO 后）；high-tier 不动；写读一致性由 NOT_READY 轮询 + wait_local_write 现成语语兜底；顺带根治未落盘大对象 pin C 残余路径（无须内存源分片）；释放 1GB 预算；联动清理清单（缓存分支移除/cache="low" 降级 no-op/QA read_cache 系列改写/结构物理删除分步）。

---

## 2026-08-29 (1): L0-L3+L1 全层实施落地——实现记录与落地修订

**chunked-transfer-design.md §13 实现记录**（全层 TDD 实施完成）：L0（64 位帧头 + ISA-L CRC-64 校验层 + trailer 磁盘格式 + 零容忍语义）/ L2（META + 4MB CHUNK 字节切片流 + DIGEST 根 + 在线块重传）/ L3（ChunkSource 拉取源 + 接收线程 + 有界队列 + Unpickler 增量消费）/ L1（DataWriter 增量 API + FlyStream sink 写 + finish_and_commit）全部落地；§4.5/§8/§9 补落地修订三节（实现与计划的偏差及理由）；§9.5 PEP574 尝试结论为负（pin 5 因 PickleBuffer 兼容回退；实测协议 4/5 in-band 内存特征一致，L1 流式化后 in-band 无瓶颈，OOB 关闭）；§13.1 十项实现问题与修复、§13.2 风险项、§13.3 设计决策记录（不确定项如实说明）。验证：单测 73/73 + 全量 QA 165/165 ×3 轮。

---

## 2026-08-28 (4): P0-3 分块/流控/背压必要性调研 + 分块设计文档

**① emir-capability-gap.md P0-3 调研结论（代码实证）**：按 fly 内网批处理负载画像分层裁定——**credit 窗口流控不做**（每连接同步 request-response 单 in-flight + 接收即消费，速率耦合在 TCP 窗口，无持续失衡土壤）；**背压结构性已含大半**（epoll oneshot + slot=4 + send 队列深度 ≤ 活跃连接数，残余仅慢客户端 HOL，`data_server_threads` 可调）；**分块唯一长期真实价值 = 内存有界化**（整取链 2C+R，GB 级对象 ×4 并发 ≈16GB 先撞内存墙），绑定亿级触发缓议。

**② 新增 chunked-transfer-design.md**（设计调研未立项）：磁盘格式已分块（4MB 独立压缩块 + 按需解压 streambuf + FlyStream 双模式），整块边界仅三处（写累积+commit 整拷贝 / 网络整帧 / 读整解压整 loads）；分层方案 L1 写直写落盘（idx 事务段容错半成品）→ L2 网络分片（DATA_RESPONSE_META + N×DATA_CHUNK，server pread 直发）→ L3 读流式消费（Unpickler 增量构建）→ L4 块级寻址（远期）；量化后 client 峰值 3GB→≈R。同步更新文档地图（README.md）。

**③ 帧长 uint32 裁定为正确性缺陷独立先行修复**：64 位帧头 = 48 位长度（256TB 上限，无尺寸政策猜测）+ 16 位校验位（魔数⊕长度折叠，垃圾帧头 65535/65536 概率拒绝，单比特翻转确定性检出）；消除 4GiB 静默回绕与 client 256MB 假上限。后追加：**ISA-L CRC-64 端到端载荷校验 + 工业零容忍语义**（校验错误一次重传→FATAL→task 失败，用户裁定）；CRC 实测 14.6 GB/s（ISA-L PCLMUL，比软件快 7×、比 CRC32C 快 1.5×，seed 链式增量已验证）；**校验调用经 `data_checksum()` 包装层收口**（用户裁定：接口稳定、实现可整体替换，契约测试锚定接口语义）。
**④ 分 chunk 校验直接落地（2026-08-29 用户裁定）**：chunk 头写时嵌每块 CRC（`[i32][i32][u64 crc][data]`，8B→16B），所有解压消费点按块验证——校验锚定写入时刻、覆盖数据全生命周期（含磁盘与缓存驻留）；逐块与整块吞吐实测等价（±2%）；不做块级定位（裁定无意义）；磁盘格式不考虑版本兼容（旧 db 读出即显式 CHECKSUM 失败）。重取语义统一为对象级：CHECKSUM → 失效缓存 → 一次重取（远程换副本优先/本地重读盘）→ 仍败 → FATAL + task 失败。实施计划定稿于 [frame-integrity-impl-plan.md](frame-integrity-impl-plan.md)（待批准）。

**⑤ 读侧并行架构 v2 + L2 协议细化（2026-08-29 用户质询驱动）**：原拉取式流式读**不是真并行**（单线程分时复用，反序列化慢于网络时 TCP 流控卡停发送方——不符合用户「网络 io 流不停止」要求），修正为**专用接收线程（纯 C++ 无 GIL）+ 有界已验块队列（~64MB 压缩态）+ 消费线程**——真线程级并行；消费不及时由队列吸收、持续慢消费才 TCP 平滑降速（代价为零：消费是瓶颈时关键路径不变）。L2 协议细化：CHUNK 帧带 seq + 帧 CRC，`CHUNK_RESEND(seq)` **在线块重传**（同连接流不断，每块上限一次，再败升格 FATAL——用户裁定）；分片发送封装为**单个自含 SendTask**（n_send≥2 全局队列下按块入队会乱序写同 fd）；三层校验分工（帧头 check / 传输跳帧 CRC / 磁盘内嵌块 CRC 写入锚点）。L1/L3 可行性经代码实证确认（WBQ 闭包单元 + high_watermark 背压现成、段事务 API 现成、wait_local_write 语义现成、`_read_streaming` 为接入缝），用户初版方案成立。

**⑥ ObjectHeader trailer 化（2026-08-29 用户裁定）**：record 格式从 `[ObjectHeader][Chunks]` 改为 `[Chunks][ObjectHeader]`——header 尾置使流式写**全程纯追加**（消除占位+seek 回写；`ios::app` 句柄下 seekp 本就无法重定向），total_size/chunk_count 写完末块自然已知；**trailer 兼作 commit marker**（崩溃残块无 trailer，结构上不可误读为完整对象）；idx 的 offset+size 即起止区间，**idx 格式零变更**；读取侧改为尾部解析（约 5 处消费点），chunk 走读恰耗 `size−trailer_size` 增强结构校验。与块 CRC 同批格式变更。

**⑦ 两文档合并 + 全量实施计划定稿（2026-08-29）**：`chunked-transfer-design.md` 与 `frame-integrity-impl-plan.md` 合并为单一文档（后者删除），并补齐 L2/L3/L1 实施计划至与 L0 同粒度（改动点 43 处编号连续、测试 48 个、每层四步验证门）。全程序：**L0 前置层**（帧头/校验层/trailer+块 CRC/零容忍，六步）→ **L2 分片发送**（META+CHUNK+DIGEST 尾帧根摘要单遍计算 + CHUNK_RESEND 在线重传 + 自含 SendTask）→ **L3 读流式 v2**（ChunkSource 拉取源抽象 + 接收线程/有界队列 + Unpickler）→ **L1 写流式**（DataWriter 增量 API + 压缩流逐块入 WBQ + 完成点注册）。README 地图同步合并条目。

**⑧ L1-L3 可行性与影响面代码验证（2026-08-29）**：三层实施计划补「现有功能影响与兼容」小节（chunked-transfer-design §7.4/§8.5/§9.4）。关键结论：**L1 适用边界修正**——backup/merge 写侧输入为整块压缩数据（do_backup_write:440），无序列化流可切，走现有整块 API（增量 API 是新增非替代）；**task 事务段兼容**（段由 mark_write_begin/end 在 task 首尾控制，abort 三步清理对块级队列兼容）；**池持有期安全**（fd in_use 标记防 TTL 误清，release 可跨线程）；**FlyStream Python file-like 面已就绪**（read/readinto/readline 零拷贝导出，Unpickler 直接可用）；**merge_db 收益修正**（用户质询驱动：merge 是压缩字节级中转——read_raw_compressed→零解压直写，L3 无收益；L2 收益在 server 传输段；新增 merge 流式中转扩展点=L2 块流直喂 L1 append_chunk，中转端峰值 C→块级）；**QA 覆盖缺口**（现有对象 ≤10MB < 64MB 阈值，流式路径需新增大对象 case）。

**⑨ L1 尝试性增强入计划（2026-08-29 用户裁定）**：pickle 协议 5 消除 numpy 数组逐数组瞬时拷贝列入 L1 §9.5——**尝试实现但不强制要求完成**：① 主写路径显式 pin HIGHEST_PROTOCOL；② 实测大 ndarray dump 峰值内存验证协议 5 in-band 免拷贝；③ buffer_callback 旁路为可选深水区（复杂度超预期即放弃）。完成 ①+② 即算尝试完成（结论无论正负记录），不阻塞 L1 合入与后续层排期。

---


---


---


---


---
---

## 2026-08-28 (3): master 寻址 .fly_config 化 + worker 正常/异常退出显式分派

**① master 寻址写入 .fly_config（首写完备，用户裁定）**：`Master.start()` 尾部
即落盘（P1），launch_local/launch_ssh/expect_workers 入口幂等重写（P2 定稿点）
——内容 = Config 全量快照 + `master_host`（advertise 可达地址）+ `master_port`
（定稿端口）。local/ssh/bsub 任何类型 worker 任何时候读取都拿到完整寻址；
launch_* 的 worker cmd 不再携带 `--master-host/--master-port`；
**`launch_ssh_workers` 彻底删除 `master_host` 参数**。advertise 规则：
`master_advertise_host` 覆盖 > 显式 bind > UDP connect 出口 IP 探测（环回
校验拒绝——hostname 解析 127.0.1.1 不可访问）> 127.0.0.1+WARN。
`Master` 默认 bind `0.0.0.0`（全接口，「IP 可访问」前提）。CLI
`--master-host/port` 保留为调试覆盖口（优先级最高）。`Config::save_to_file`
改原子写（tmp+rename）。新 config 键：`master_host`/`master_advertise_host`
（str）、`master_port`（int）。

**② worker 正常/异常退出显式分派（双侧，用户裁定）**：
- worker：`ExitReason` 枚举（MASTER_SHUTDOWN/LOCAL_STOP=graceful；
  MASTER_LOST/REGISTRATION_REJECTED=abnormal），`initiate_shutdown` 显式分支
  （graceful=INFO+发 `WORKER_EXIT`（新增消息 62，入 serialized domain 保证
  先于同连接 DISCONNECT）+退出码 0；abnormal=ERR+退出码 3）。
- master：`on_disconnect` 三分派——shutdown_pending ∪ exit_confirmed →
  `handle_worker_exit`（终态 **EXITED**：无判死告警、无数据全灭 fail）；
  drain 期未标记/断连即死 → `handle_worker_death`（DEAD）；其余 → 宽限。
- `WorkerStatus` 新增 **EXITED** 终态；活体判定统一正向谓词
  `worker_status_alive`（5 处 `!= DEAD` 负向枚举消除）；心跳扫描跳过 EXITED；
  monitor GUI 的 exit_kind 由启发式推导改为事件直读（EXITED/DEAD 原生落库）。
- 单测：新增 GracefulExitClassifiedAsExited / ExitCodeReflectsExitReason；
  两个宽限家族用例的断连模拟从 `worker.stop()` 改为
  `simulate_master_disconnect_for_testing`（stop 现在声明退出，与宽限语义相斥），
  并补「宽限表非空」确定性等待。

**验证**：单测 51/51、QA 回归 19/19（fault/network/api/dependency）、
ssh config 引导全链路通过。

---

## 2026-08-28 (2): launch_ssh_workers —— SSH 多机 Worker 启动落地（roadmap F1 完成）

**框架 API（fly.launch_ssh_workers / Master.launch_ssh_workers）**：通过 ssh 在
远程主机启动 `fly --worker`（每 target 一个 worker，字段 host/attributes/role/
host_alias）。与 launch_local_workers 同构：先登记注册占位符再下发（防注册
竞态泄漏占位符）、共享 `.fly_config`、复用 `_find_fly_binary` 探测。

- **生命周期不持本地句柄**：远端 nohup 后台化 + 三重重定向（stdout/stderr→
  远端 worker{N}.log、stdin 断开），ssh 会话立即返回；worker 退出靠框架消息
  （master stop() 广播 ShutdownMessage 自杀 / master 失联心跳超时自退）；
- **路径约定**：fly_binary/log_dir/config 要求 master 与远端一致（localhost
  自连、共享存储成立），异路径显式传 fly_binary；跨机必须显式 master_host
  （默认绑定地址 127.0.0.1 仅自连有效）；
- ssh 失败抛 RuntimeError（占位符无法回收，终止本次 run 的处置口径同 bsub）。

**测试**：`qa/network/test_launch_ssh_workers.py`——localhost 自连环回全链路
（前置免密检测→启动 2 worker 注册→write/read 数据面→stop 后远端进程退干净）。
环境要求 sshd + 密钥免密（配置指引在测试头注）。

**验证结论**：QA 3/3 过（新 case + wait_workers_registered + process_workers
回归）；手动端到端验证 requires 属性调度匹配。发现既有现象（非本次引入）：
显式 stop() 时 drain 阶段把 worker 主动断连报为 `worker dead + 副本全灭`
ERROR——launch_local_workers 同脚本复现相同输出，属 stop 时序噪声，待立 issue。

**文档**：python-api/module.md（符号总表 + API 权威小节）、architecture.md/
roadmap.md/remaining-todo.md F1 状态 ✅、emir-capability-gap.md P0-1 更新
（剩余差距收敛为「跨机实测」）。

---

## 2026-08-28 (1): 文档一致性治理 —— 事实修正 + 「一处权威，他处链接」收归 + EMIR 能力差距文档

**背景**：全库文档-实现一致性审计发现三类问题——数字快照失真、历史文档被取代未标注、
同一数据在多文档复制漂移（消息类型数量曾同时存在 33/40/54 三个版本且全部过时）。

**事实修正**：
- 消息类型：AGENTS「33」/CLAUDE「40」/architecture「54」三个过时数字全部移除，统一链接权威表；
- runqa 默认并行「4」→ 实为固定值（2026-08-16 已裁定，AGENTS 同步）；
- CLAUDE.md §6 导出列表补齐滞后（缺 ensure_workers/merge_db/Project 族等）；
- `__fly_db__:{db_id}:...` 旧编码残留（python-api/project-design/monitor-design）→ `__fly_db2__:{uid}:{db_path}`；
- restart_failed_tasks 单 bin 直传旧签名 → 传 db/db_path/list（§15.4 口径）；
- NEW_MODULE_GUIDE：删除 `__all__` 与 try/except 双布局示例（均为禁用写法）、`EXP` 前缀 → `EX`、
  §6.4 改为 fly.sh do_install 统一循环；
- network/module.md 删除已废弃 IOThreadPool 章节；qa/README dual_output/glob 描述修正；
- push-hook.md `-j 2` 残留与脚本实参 `-j 6` 的矛盾统一；
- db-merge-design.md 加「迁移追踪部分被 db-chain 取代」头注；message-system.md 加枚举快照注；
- CLAUDE.md/AGENTS.md 模块表补 message（及 CLAUDE 补 solver/fly/monitor 行）；
- docs/README.md 地图补 issues/008、009、coverage-report、run-summary 四份缺失文档。

**「一处权威，他处链接」收归（权威落点 + 链接化）**：
- 消息类型语义全表 → network/module.md「消息类型总表」（唯一权威，不写数量，只维护语义分组）；
- `from fly import ...` 公开符号总表 → python-api/module.md「公开符号总表」；
- runqa 并行度/发现机制/case 总数口径 → qa/README.md「并行度权威口径」；
- 文档约定固化入 docs/README.md「文档约定」：统计性数字（随开发漂移）一律不写死、指向权威源码或权威表；
  被取代的历史设计文档不删、文头加取代注。

**新增 [emir-capability-gap.md](emir-capability-gap.md)**：面向分布式 EMIR 工具的框架能力
现状 + 差距分析（P0 规模/部署、P1 数值内核、P2 工程化），作为后续演进参考；
docs/README.md 与 AGENTS.md 文档表收录。

---

## 2026-08-27 (5): ensure_workers —— 向 master 申请现有 worker 并追加指定属性（issue 009 根治）

**框架 API（fly.ensure_workers / Master.ensure_workers）**：向 master 申请
N 个现有 worker 并分别为其**追加**指定属性（不启动新进程；workers 为属性
集合 list，长度即申请数，元素允许 str 单属性简写）。

- **两阶段收集**（timeout 默认 10s）：时限内缺口只从空闲候选补齐；到点仍
  未齐放宽到忙碌候选——给 BUSY worker 也打上属性，不等其空闲，后续 task
  由调度系统按 requires 自动派发；
- **静态预检立即失败**：排除后的全量池（IDLE+BUSY）盖不住申请数时直接抛
  RuntimeError 带明细，不消耗时限（用户裁定：池子本身不够时不做无意义等待）；
- **exclude 正则保护**（re.search 任一属性命中即排除出候选池）：并发求解
  flow 排除已被其他 flow 编队的 worker，防止碰撞；
- 幂等：重复调用同规格经盘点直接命中，不重复分配、不下发消息。

**新增下行消息 WORKER_PROPERTY_ASSIGN=61**（此前 master→worker 无任何属
性设置通道，属性只能 CLI 启动参数）：worker 侧去重并入自身 attributes_ 后
沿既有 WORKER_PROPERTY_UPDATE 上行回报——master 能力视图与调度唤醒零新增
链路。WorkerManager 新增盘点/候选原语并导出：count_workers_with_all_
capabilities / get_busy_workers / get_worker_capabilities。

**编队属性命名单点规范（双 flow 防碰撞）**：SolveDb 新增 `worker_attr(tag)`
→ `"rasg:{uid}:{tag}"`（uid 跨进程持久于 _DB_META）。并发 flow 各持不同
uid → 属性零交集；task requires 完整字符串精确匹配不串池。solver 的 sd_i/
check 属性全部改经此生成（前缀改动只改一处）；kickoff 改为「进程数量先行
补空属性 + 注册就绪等待 → ensure_workers 打编队」，替代旧"带属性 launch +
只数连接数的 wait_for_workers"，issue 009 两处脆弱点（空属性 auto-spawn
抢位 / 手工复刻编队隐式契约）根治。

**executor 时序根治**：preprocess 先 `_resolve_func`（导入 task 模块、完成
包副作用的 `_ROLE_REGISTRY` 注册）再 `deserialize_args`（db 参数按 meta
role 重建子类）——此前导入晚于反序列化，worker 首个 solver task 收到的 db
退化为基类实例，子类成员（load_solution/worker_attr）不可用。role 已知但
未注册时降级 WARN（承载包无人导入的残留场景）。

**QA**：qa/scheduling 新增 test_ensure_workers.py（分配/幂等/追加/exclude/
预检 fail-fast）、test_ensure_workers_busy_relax.py（BUSY 放宽 + 调度接管
端到端）、test_ensure_workers_dual_flow.py（双 db 双 flow 物理隔离 + 精确
调度）。rasgd_restart_run2 移除手工编队规避，改 load_db 先行 + 数量补齐 +
ensure_workers（顺序敏感性回归）。

**等待边界统一（裁定：所有等待受 ensure 声明的 timeout 约束）**：
- 已唤起未注册的占位 worker 计入静态预检容量（假定其属性不被 exclude 命中），
  注册等待在 timeout 之内；在册池已满足申请时零等待立即返回——kickoff/
  flow 侧不再有独立的 settle 等待；
- **原子快照根治采样竞态（stability R3 实锤）**：新增 C++
  `MasterAgent::snapshot_worker_pool`——注册路径持 expected 锁跨越
  「占位符转正 → 进 WorkerManager」全程，采样同锁单点完成，过渡态
  （两边都不在）对容量口径不可见；此前"在册池 + 占位符数"两次独立采样
  会把过渡态漏计，容量瞬时少计导致预检误判池不足。快照按 worker 全量
  返回 capabilities（空属性 worker 是补拉候选主力，不可丢条目）；
- **本地 spawn 的注册前早夭快速失败**：`_wait_spawned_workers(batch_ids)`
  轮询本批 Popen 句柄，「已退出且未注册」立即 RuntimeError（资源饥饿/
  启动即崩不再挂死）——`worker_register_timeout=0` 的无限等待语义仅保留
  给无本地句柄的外部唤起（bsub/expect_workers）。qa/api 新增
  test_spawn_early_death.py。

**稳定性 100 轮（163 case × 100，连续通过口径）两处 QA 修复**：
- qa/monitor/test_monitor_db：object_io 明细断言去掉无 ORDER BY 的位置性
  `LIMIT 1`（MONITOR_TASK_IO 异步成组上报，落库次序随批次翻转，R23 实锤
  obj_mem 抢在 obj_plain 前）→ 按对象名定点断言存在性（明细行 bytes=0 属
  正常，字节口径聚合在 tasks 表）；运行中实时只读连接加失败重开 + 15s
  有界重试（PERSIST journal 写者 commit 持 EXCLUSIVE，高负载轮 5s
  busy_timeout 不够，R76 实锤 database is locked）。

---
---

## 2026-08-27 (4): dynamic 求解器三阶段重构（setup/solver/收尾清理）+ RPC 常驻 service 线程 + PeerRpc GIL 释放

**结构演进（用户逐轮裁定收敛）**：v4 每步短命 task 组 → 事故链暴露 agent
级请求队列"多消费者互抢"结构性缺陷 → 裁定"拒绝全时间步单 task 形态（牺牲
task 检查点特性）"后定稿为**三阶段架构**：
- 阶段 1 setup：LDLT/子域、粗校正 LU、listen 端口与 channel 池一次性建立
  入 worker 进程缓存；缓存 key 分层——数据按 matrix_ref（重投不重做分解）、
  连接按 gen（换代重建隔离重投窗口）；
- 阶段 2 solver per t：**连接方向反转**（compute listen / check 主动连接，
  "该连未连"时序洞消除）；compute = 参数注入短命 task + 常驻 service 线程
  是唯一消费者（实测多消费者互抢队列事故）；check 驱动迭代，存活不变式
  alive<nsd 即组死；
- 阶段 3 controller（ras_check 绑定与 check 同 worker）：推进/收尾清理
  （cleanup × nsd 销毁成员缓存/server/属性）。

**框架修复（agent_export.cpp）**：`peer_rpc_recv_request` / `peer_rpc_call`
绑定时长阻塞却持有 GIL——service 子线程进入 wait(0) 后带 GIL 冻结全场，
主线程 Thread.start() 永不返回（faulthandler 栈取证）。补
`gil_scoped_release`（RAII 块级，返回打包前自动重获）。同款隐患其余绑定
按需后续排查。

**timeout 裁定落实补遗**：QA run1 的期望失败检测改 master.failed_tasks
权威轮询（wait_tasks "队列空即返回"与失败落账存在竞速窗口）；get_dynamic_result
删除 timeout 形参。

**已知限制立项（docs/issues/009）**：三阶段引入跨 task 进程级缓存 ⇒ 重投
场景属性编队成为正确性前提（旧版无进程态因此无需）；当前以 QA 手动建池 +
launch 先于 load_db 规避；框架增强方向（角色 worker 就绪原语）用户裁定后续
实施。

**验证**：dynamic QA 全套绿（含 restart 全新 run 断点续跑——组死传染/re投
重启/no-op 收敛/结果 md5 一致）；qa/solver 家族绿；单测 69/69。

---
---

## 2026-08-27 (3): timeout 裁定落实——dynamic 求解器数据规模相关等待全面无限化 + T=1≡单次求解等价验证

**用户裁定**：所有 API 慎用 timeout；数据规模相关的等待（读大对象、
链推进、资源就绪）设固定值会在大规模数据网络 IO / 集群调度排队时触发
非期望失败——历史明确提示过的误用模式。此前 2e6686a（等待无限化）/
load_db PendingIdxLoad 无 deadline 均同源先例。

**dynamic 求解器四处整改（我此前引入的误用，全数清除）**：
- get_dynamic_result：删除 timeout 形参（API 表面不再提供诱导误用的
  参数）；失败语义由 wait_obj can_still_produce 兜底
- _connect_with_retry：删 60s 总窗口 → 无限重试（check task 就绪时刻
  取决于调度排队）+ agent.is_running() 关机逃生口；group.connect 内部
  wait_obj 显式 timeout=None（PeerChannelGroup 默认 60s 同属误用）
- chan.rpc：删 per-request 60s 固定超时 → 无限阻塞——对端 task 死亡由
  **断连事件**驱动唤醒（listener 关闭 → rpc FAILED），事件而非计时器；
  单轮时长由最慢子域决定，大矩阵 LDLT 分钟级下任何固定窗口都误杀
- check 收齐循环：accept_one 5s 轮询窗 → 纯阻塞（保留陈旧断连事件
  容错吞掉——agent 级队列共享，上组 close 残留事件不得当致命错误）
- QA 脚本自设的 wait_tasks(timeout)/get_dynamic_result(timeout) 全部
  移除；卡死防线归 harness subcase 窗口（测试基建职责）

顺带修复（reuse 对照实验暴露的真实 API bug）：get_dynamic_result 的
wait_obj inputs lambda 曾声明必选 timeout 形参 → 无参调用 TypeError。
现有 QA 全部显式传参掩盖了此路径；early_stop 场景改为无参调用作回归锚点。

**T=1 ≡ 单次求解等价验证**（用户指出的复用正确性）：同一 n500 矩阵，
v2 直跑 vs solve_ras_graph_dynamic(num_steps=1)：iters 同为 7、rel_res/
rel_err 全同、**解向量逐位一致（max|Δ|=0.0）**——理论推断成立，实验
确认无隐藏行为分叉。

---
---

## 2026-08-27 (2): dynamic 求解器收敛判定改残差主导（coarse）+ warm start 效果标定

**warm start 标定（.work 实验，n500 coarse tol=1e-5 min_steps=2）**：收益
取决于相邻步 RHS 变化幅度——均匀 +0.1%/步或不变 [5,3,3]（-40%）；用户
指定的真实形态（10% 节点 +30%/步，其余不变 / 热点方块 ~5% / 10%大变+
40%微变+50%不变三组稀疏场景）一律 [5,4,4]（-20%）。结论：迭代轮数由
"扰动传播所需最少轮数"决定（RAS 边界不动点），不变占比再高也省不掉
传播链；均匀大变化场景无收益。

**收敛判定改造**：check（coarse 模式）改为残差主导——r_rel =
‖b_t−A·x‖/‖b_t‖ < tol 即收敛，compute 的 Δx 增量标志退出 coarse 收敛
判定（非 coarse 无 A_fine 沿用 flags）。理由：残差是相对扰动右端项的
解误差直接界，Δx 只是迭代停滞信号——warm start 的准静态初值下两类
口径可能分叉（flags 早停损精度或空转）。

**A/B 实测（tol=1e-8，min_steps=1，uniform d10 / hotspot 稀疏 /
uniform d001 三组 vs splu 精确解验收）**：iters 与增量判定完全同轮
（[7,6,6]/[7,5,5]/[7,5,5]），rel_res ≤7.5e-9 全部 <1e-8 验收线，
rel_err ~3e-11（余量三个数量级）。结论：1e-8 目标下迭代瓶颈是 RAS
边界传播本身而非判定口径；改动的价值是收敛语义可解释（直接保证
r_rel<tol）+ 为精度-成本调优提供准确旋钮。user 验收通过。

---
---

## 2026-08-27: Dynamic 多右端项连续求解（EmIR dynamic IR drop）+ 三项框架根因修复

**需求（用户多轮裁定）**：同一矩阵连续求解一组右端项（G·x_t = b_t，T 个
时间步，b_t = f(x_{t-1})），每步结果入库。裁定要点：单步迭代在长 task 内
RPC 直连（v2 思路）；不同 RHS 间 task 隔离——**核心目的是失败重跑原生
支持**（单步失败不丢已有结果，restart 只重投失败部分）；restart 可能是
全新 run（worker 无缓存），task 设计冷启动安全；**master 永不阻塞**
（编排由 task 链自驱动）；warm start 默认启用；worker 用 attributes +
高优先级（90）钉住复用矩阵缓存。

**新代码**：`src/solver/py/ras_graph_dynamic.py`——kickoff/compute/check/
controller 4 个 task + `solve_ras_graph_dynamic`（非阻塞 kickoff，立即返
回 handle）/`get_dynamic_result`（wait_obj 等终止标记）。链：kickoff（coord
预分块）→ step 组（compute×nsd 钉 sd_i + check 钉 ras_check，RPC 迭代到
收敛）→ check 收敛时提交 controller（读 x_t → update_rhs 生成 b_{t+1} →
提交下一组）→ 链闭合。设计细节：gen 会话前缀防跨 solve 进程缓存污染；
warm start 从 sol_{t-1}（持久，restart 权威）提取 ghost/初值；b_t temp
由 controller 逐步清理；收敛段 respond done 先行（reactor 异步 send 需
写库往返作缓冲，否则紧随的 listener.close 掐死未发出的 respond——实测
n20 下 8 轮 RPC 9ms 内完成即触发）；check 收齐循环按 sd 去重收满 + 断连
事件吞掉（agent 级事件队列会残留上组 stop 的断连，误读为致命错误炸死
下一组）。

**三项框架根因修复（QA 压测暴露）**：
- **master_agent.cpp on_master_remove**：master 自写对象被 worker remove
  时 master 进程的 ObjectCache/local index 未清——master read_object 仍从
  low cache 命中已删对象。补 remove_local_index + ObjectCache.remove。
- **worker_agent.cpp on_idx_load_command**：只写过 temp 对象的 writer
  （正式 idx 空，如编排链 kickoff/controller 的输出全是 temp）不进
  loaded_writer_ids → master rebuild 跳过 → 其 temp 对象在调度视图中不
  存在 → 重投 task 依赖判 Unresolvable。temp 恢复非空也上报。
- **callable task 参数 cloudpickle 序列化**（task.py/executor.py，~15 行）：
  `__fly_cfunc__:` 标签——controller 持有用户 update_rhs 回调（脚本内
  闭包/lambda）随参数传 worker。py_test callable_args_test（7 例）。

**验证**：QA 新增 test_ras_graph_dynamic（4 subcase：basic 数值/warm
start/LDLT 缓存复用恰 nsd 次/controller 数据流；early stop；restart 失败
注入 run1 + 全新 run 断点续跑 run2——重投恰 5 个失败组、已有结果 md5
一致、链恢复至完成）；全量单测 69/69；QA 159/159。实测 n20/nsd4/T3：
kickoff 0.59s 非阻塞返回，全程 2.9s，iters=[9,8,8]（warm start 生效）。

---
---

## 2026-08-22: 运行时 Summary 统计增强（RunMetrics + 机器信息日志 + 心跳成组补发）

**需求（用户提出 + 四轮 review 裁定）**：退出时无运行信息 summary；需
1)运行时长 2)总时长 10 等份分阶段的集群物理内存 avg/peak（综合 master+全部
worker，只报集群总量）3)按 db 的磁盘用量/创建时长/创建期间内存 avg/peak
4)每 worker 每 10s INFO 机器信息（proc_rss/host free/total/cpu%/loadavg）。
设计与全部裁定记录：`docs/run-summary-metrics-design.md`。

**代码变更**：
- `core/cpp/system_info`：数值 API 公共化（process_rss/hwm_bytes、
  host_mem_bytes、host_loadavg_1m）；现有格式化函数复用。
- `main.cpp resource_monitor_loop`：5s/DBG/仅 cpu+rss →
  `machine_info_interval_seconds`(10s)/INFO/补齐 host 内存与 loadavg；
  master+worker 共用。
- `HeartbeatMessage`：+`rss_epoch_ms_[]/rss_bytes_arr_[]` 平行数组（真实
  epoch 毫秒——用户裁定相对时间戳完全不准）；worker 侧发送失败样本缓冲
  不丢、下次心跳成组补发（单线程独占无锁）。
- `agent/cpp/run_metrics`（新）：tick 骨架 + 最近邻合成（std::upper_bound，
  用户裁定按 master tick 间隔合并最近样本即可）；判死 epoch 截断/复活
  重计；db 三钩子（freeze 时锁外 du -sk 统计终值、merge 作废退出补测、
  data_dir 独立目录加算）；`metrics_tick_seconds`(10s)。
- 输出（第四轮裁定）：直写 `{log_dir}/runtime.summary` + `db.summary`
  （独立 ofstream 不经 Logger），用户日志仅 `FLY::0002` 一行（耗时+路径，
  MessageRegistry 注册点 master_agent.cpp start）。
- master 挂钩：on_heartbeat 成组样本、handle_worker_death 判死、
  db 创建双入口（register_database + **get_or_create_database**——e2e
  实测 open_db 走后者，漏挂则 db.summary 空）。

**同批两个根因修复（用户裁定追加）**：
- **MSG 未注册 id 不再静默丢弃**（用户裁定：不可默认丢弃且无提示）：
  message_macros.h 的 get_level 失败分支打 WARN（立即 flush 通道）后丢弃
  ——未注册是编程错误，与配额超限的设计性静默不同。单测
  UnregisteredMsgEmitsWarning。
- **"退出期 Logger 吞 INFO"根因根治**（此前待专项，本次 [SD] stderr 取证
  收窄 + 修复）：`sys.exit(run())` 的 SystemExit 在嵌入式 CPython 的
  PyRun_SimpleString 内部走 handle_system_exit → Py_Exit → **exit()**——
  main() 中 PyRun 之后的代码（含 Logger::shutdown）**恒定不执行**（master/
  worker 同）；Logger 对象 leak-on-exit（P3-18）永不析构 → 退出前 ≤1s 的
  INFO/DEBUG 缓冲全丢（WARN 写时立即 flush 幸存）。"偶发"假象 = 多数 INFO
  撞得上 1s 惰性 flush，只有退出瞬间附近的丢。修复：**Logger::shutdown
  注册 std::atexit**（exit() 必经，覆盖一切退出路径；幂等）。验证：修复后
  master/worker 日志尾部 `_cleanup stage` 系列行（历史上从未落盘）全部
  完整。顺带澄清 2026-08-19 压测"Logger 实例偶发吞 INFO"现象即此根因。

**验证**：新增 system_info_test(4) + run_metrics_test(11：render 纯函数/
最近邻生命周期/成组计数/db 生命周期/du/文件写) + master_agent_test 2 例
（StopWritesSummaryFiles/HeartbeatGroupedRssSamplesCollected）+
message_registry_test 1 例；全量单测 66/66；QA 149/149；e2e 手工验证四项
需求 + MachineInfo + FLY::0002 + 日志尾部完整性全过。

---
---

## 2026-08-19: 关闭语义双通道重构 + 压测暴露的三层根因修复（50 轮稳定性 50/50）

**代码变更**（f146043 → 945e213 六提交链）：

- **关闭语义双通道（用户裁定，57a9602）**：正常 `stop()` = drain 等全部
  RUNNING task 完成（`drain_timeout_seconds=600` Config 兜底，超时转 fast
  善后，0=无限逃生口）；SIGTERM / graceful_exit = `fast_exit(reason)` 立即
  fail 善后（failed record 留痕）+ 广播 `STOP_NOW`（新消息 58）——worker 收到
  即 `kill(getpid(), SIGKILL)`，不依赖 master 知 pid/句柄（bsub/ssh 跨机
  worker 同样生效）。30s 硬超时废除（长 task 等不到只是延迟处死且超时后
  RUNNING 无留痕）。worker `initiate_shutdown` 主动 close master 连接。
- **drain lost wakeup（945e213，4 实例压测 [SD] 取证）**：`notify_drain_if_active`
  无锁 notify_one——旧 30s 超时掩盖多年。同批扫描修复全仓 9 处无锁 notify
  （write_back_queue 三处/data_client_pool 两处/master 四处，详见
  DEVELOPMENT_GUIDELINES §13.2 增补）。
- **Worker 启动时序（用户裁定的结构性修复）**：`send_register_message` 后移
  至全部初始化完成后（dup ack 只作用于完整初始化的 worker）+
  `shutdown_state_mutex_`（start 尾段与 initiate_shutdown 标志写互斥）+
  poll_task 在 executor 未注入时不弹出普通 task（原实现静默丢弃）。
- **db_path 失败响应 key 断链**：master 对 unknown db 清空 `response.db_path_`
  → worker 以该字段匹配 pending 永不命中 → 稳定等满 5s
  （OnDbPathResponseFailure 5159ms→149ms）。
- **性能（用户裁定单 case >10s 后收紧 5s 必查）**：master_agent_test
  150.7s→11.0s（drain 30s×3 + 连接不关 10s + fake conn 白等 40s 三层）；
  全部单测 case <5s（benchmark 除外）。

**文档同步**：

- `docs/agent/module.md`：关机流程重写为双通道语义（stop/fast_exit 对照）；
  新增「Worker 启动时序（半初始化竞争防护）」小节（注册消息必须在初始化
  完成后发出 + poll_task executor 约束）
- `DEVELOPMENT_GUIDELINES.md 13.2`：无锁 notify 事故清单补 945e213 批次
  （drain/workers_drained/WBQ/client_pool 九处），新增排查工具段
  （无锁 notify 扫描 + [SD] stderr 执行链——Logger 会被 case 的 level 参数
  吞掉 INFO，stderr 永远可靠）

**验证**：61 轮 4 实例压测（--runs_per_test=4，修复前 1/3 轮必挂）+ 50 轮
稳定性 50/50（单测 61×50 + QA 149×50 零失败）。

---
---

## 2026-08-18: 超长函数三连收口 + schedule_tasks assign 出锁事故（发现→根治→回归钉死）

- **schedule_tasks**（master_agent.cpp，144 行）：提 compute_locality_hints()
  （锁外预计算）与 fail_and_persist_tasks()（依赖不可解/属性死锁同构收尾）。
  **中途事故**：曾按计划把 assign 循环移出 schedule_mutex_（缩短持锁），
  全量 QA 3 case 失败（test_golden_n50_sd9 等）——决策即 graph_->remove_task、
  RUNNING 登记在 assign 尾部，出锁后「依赖不可解检测」在并发轮次看到
  「ready 空 + 无 RUNNING」的决策瞬态，把 pending 链整批误判 Unresolvable
  （master.log 实证 Task 101010/100010 被误杀，RAS 链断在 step 101/1）。
  根治 = 决策/assign/检测必须同临界区（assign 留锁内，缩短持锁靠 locality
  预计算锁外），锁内注释完整记录不变式；新增回归测试
  UnresolvableDetectionDoesNotFireDuringAssignFlight（send 钩子 + cv 有界
  交错，临时复刻 buggy 结构验证过确实转红）。调试中间踩坑：fly.sh install
  只 symlink 不构建，复测跑过旧二进制一度误导「修复无效」——二分定位揭穿，
  显式 buildonly //src/main/cpp:fly 后 3 case 全过。
- **read_raw_compressed**（data_service.cpp，149 行）：提 read_tier1_hit()
  （TIER1 装配：二次索引查询+temp 分流+py_name/hash 解析）与 try_tier2_read()
  （TIER2 全循环：退避/期限/副本踢除/backup suggest），主函数收敛为 ~45 行
  TIER1→TIER2→TIER3 编排。补 TIER3 回环 2 单测（此前无专测）：
  Tier3RefreshReentersTier2AndHits + Tier3QueriedGuardsAgainstBouncing
  （OBJECT_NOT_FOUND 快速清副本表避免 30s 期限拖慢测试）。try_read_local_raw
  签名不扩（DataServer serve 热路径共有，风险大于收益），二次查询封装进
  helper 并注明理由。
- **merge_db**（agent.py，272 行）：提 _ensure_merge_workers()（Phase 2
  worker 池：源 host 补齐 + master host target 池，含 _merge_worker_hostname_map
  快照 helper）与 _delete_merge_source_with_retry()（Phase 5 删源+重试+流程
  message），Phase 结构不变；12 个 merge QA 回归。
- 验证：单测 60/60 + QA 149/149（含事故 3 case 定向复跑）。
- remaining-todo §六 超长函数行收口。

---
---

## 2026-08-17 (7): 启用 WRITE_REGISTRATION_FAILED——写注册被拒的通用兜底（4 落点）

定义于 error_types.h 但生产零使用的错误码启用。产生端换值后链路自动透传
（Ack→PendingWriteRegister→last_error_type→database.cpp→Python raise）：

- **①空 hash 到达 master**（do_write_register 前置判定）：非法注册请求
  （上游 commit_write 时间戳 guard 应保证非空），原落入 provenance 空分支
  误标 WRITE_PROVENANCE_MISMATCH——语义上没有「已有 hash 的对比」。
- **②未注册窗口防御超时**（worker register_write_with_master 300s 上限
  耗尽）→ WRITE_REGISTRATION_TIMEOUT（与已注册分支 5s 超时对称，原 UNKNOWN）。
- **③worker 终止批量 fail pending 写注册**（initiate_shutdown + 测试 hook
  镜像同步）→ WRITE_REGISTRATION_FAILED（「注册未确认」的字面语义，原 UNKNOWN）。
- **④master 自写 running_=false**（on_master_register_write）→ 
  WRITE_REGISTRATION_FAILED + 错误消息（原 {"",UNKNOWN} 被调用方当成功放行，
  写未经 provenance 裁决）。
- **database.cpp 补 WRITE_REGISTRATION_FAILED 映射分支**：撤缓存 +
  on_write_failed → WriteErrorType::REGISTRATION_FAILED（原会漏过四个 if
  静默当成功）。Python database.py 映射已存在无需改。
- error_types.h 注明新语义；原设想「对象已存在拒绝」（issue 003 方案 A）
  已被 DUPLICATE_SKIPPED + provenance 体系取代，注释记录。
- 测试（TDD 先红后绿）：master_agent_test 新增
  EmptyWriteContextHashRejectedAsRegistrationFailed（do_write_register 移
  public 供测试直调失败分类）；worker_agent_test
  WriteRegisterPendingBlocksUntilReconnected 段一补终止唤醒 error_type 断言。
- 验证：单测 60/60 + QA 149/149。

---
---

## 2026-08-17 (6): throw→error code 核心残留清零（10 处消除 + FLY_DECODE 暴露点收口）

- **config.cpp set_int/set_str void→bool**（对标 get_int INVALID_INT 哨兵）：
  workers launched 后 set 返回 false + ERR 日志（值不变），不再 throw 穿越
  binding 变 RuntimeError 无人 catch。测试 ThrowsAfterWorkersLaunched 改
  SetRejectedAfterWorkersLaunched（断言 false + 值不变 + reset 后 true）。
- **tcp_connection_manager.cpp 4 处 throw 清零**：listen() void→bool（错误
  通道对称于 connect 的 0 哨兵）；epoll fd 惰性创建（ensure_epoll，构造函数
  不做可失败系统调用）；工厂未知类型返回 nullptr（原 throw 是生产死分支，
  唯一活路径 Python export）。调用方补错误分支：worker/master listen 失败
  干净退出 start()（running_ 保持 false，is_running() 可观察）、
  peer_rpc_server 删 try/catch 改 bool。新增测试 ListenFailureReturnsFalse
  （EADDRINUSE）+ InvalidTransportType 改 nullptr 契约。
- **object_header.cpp deserialize 4 处 throw→bool+输出参数**（issue 002
  review 批次 C 方案）。11 个调用点：9 处防御性 try/catch 删除；2 处原无
  catch 的暴露点补错误分支——do_backup_write 坏 header 撤登记+恢复 context
  （不落盘坏数据）、internal merge 坏 header TaskFailed。文档勘误：
  remaining-todo 原记"object_header 已清零"有误（实为 4 处仍在）。
- **FLY_DECODE 三宏 throw 保留为受控设计**（issue 002 review 已裁定分阶段；
  网络主路径由 MessageProtocol::decode catch + reactor X-3 双层消化），
  2 个真实暴露点收口：database.cpp _DB_META header 加载补局部 catch（按
  无 meta 处理，不再向上抛）；export __setstate__/__setstate_from_buffer__
  4 分支局部 catch 转 fly_export::value_error（损坏 pickle 抛 ValueError
  的 Python 惯例，消息取原始 e.what()）。新增 py_test setstate_error_test
  （截断 bytes + 坏 magic → ValueError）。
- remaining-todo §五 issue 002 行同步（含边缘 throw 保留惯例说明：
  export type_error / writer_pref_rwlock system_error / solver、
  worker_agent 启动期少量）。

---
---

## 2026-08-17 (5): 锁内 IO 欠账收尾——workers_mutex_ 锁内 send 全量快照化（19 处）+ freeze 出容器锁

- **workers_mutex_ 锁内 send → 快照模式（19 处）**：master_agent.cpp 新增
  `snapshot_worker_conns()`（广播快照，返回 (worker_id, conn_id) 对）与
  `lookup_worker_conn()`（单发查表，0=未连接）两个私有 helper；stop Shutdown
  广播、心跳判死 Shutdown、重复注册 dup_ack/probe、StorageSpawn、freeze×3
  广播、ObjectRemoved×2、MessageLimitSync、VarBroadcast、IdxLoadCommand×2、
  merge TaskAssign/DeleteData、MergeCleanup×2、MSG_COUNT_REQUEST 全部改为
  锁内只快照、锁外循环 send。send_merge_task/send_delete_data 的错误处理
  （cancel/complete）一并出锁，原「PendingRpcMap leaf 在 workers_mutex_ 内
  调用等价」注释随之删除。快照后断连竞态由 transport 未知 conn_id 安全 -1
  分支兜底（同 on_master_remove/on_backup_request 既有模式）。
- **`Database::freeze()` 移出 db_instances_ 容器锁（2 处漏网）**：
  commit_pending_frozen 与 on_database_freeze_request stream 分支——原代码
  就地注释宣称「freeze 已移出容器锁（D2 拆除）」但实际 freeze() 仍在
  shared_lock 作用域内执行（drain/marker/vars 落盘重 IO）。改为锁内只
  find+拷 shared_ptr，锁外调 freeze()，注释与行为对齐。
- DEVELOPMENT_GUIDELINES §13.3：历史欠账段（db_instances_ 锁内 send /
  do_write_register 全流程——已被 a1c210f 清除但文档未更新）改写为
  workers_mutex_ 快照模式的现行规范。
- 验证：单测 59/59 + QA 149/149 全过（freeze 族/注册/freeze 广播/merge 全链
  行为回归；锁时序无直接断言测试，以全量回归收口）。

---
---

## 2026-08-17 (4): 注册守望文档同步 + 审计行动状态收口

- core/module.md：worker 生命周期键表补 worker_register_ack_retry_
  initial_ms（职责分层说明：连接级丢失走事件驱动重连，守望仅覆盖
  应用层吞消息）
- architecture.md §4.2：Worker 线程表补 Register Watchdog Thread 与
  Reconnect Thread 两行
- agent/module.md：WorkerAgent 启动流程更新 + 新增「注册守望」小节
  （职责分层/事件驱动/退避重发/join 顺序）
- coverage-report-2026-08-16.md：§五建议 1-6 全部标注完成状态
  （含 G2/P3-24/P3-23 三个附带修复的产出记录；建议 6 确认为合理缺口）

---
---

## 2026-08-17 (3): 注册守望重构——固定间隔轮询改为事件驱动 + 指数退避

用户 review 指出 30s 固定间隔寄生 heartbeat 循环的实现丑陋（职责耦合
+ 时序粗糙：1s 配置实测延迟 10s 才触发）。重构为独立注册守望线程：
cv 等 RegisterAck（on_register_ack 持锁 notify，成功即刻退出零空转）；
超时指数退避重发（500ms ×2 上限 30s，与 connect 重试风格统一）；
reconnecting_ 期间让位 reconnect_loop；initiate_shutdown 持锁 notify
同步退出。配置键 worker_register_ack_resend_interval(30s) 废除，换
worker_register_ack_retry_initial_ms(500ms)。回归用例改 100ms 初值
（20s→10s，重发实际 ~100ms 触发）。

---
---

## 2026-08-17 (2): P3-23 根治——注册 ack 丢失的幂等重发兜底（确定性证据链）

按用户裁定流程（不接受无证据的"资源饥饿"归因；正常链路 60s 处理不完
必有逻辑问题）完成调查与修复：

- **取证**：测试内建确定性取证装置（失败时 scene 直出 gtest 输出——
  bazel sandbox 下 Logger 相对路径文件不可靠；每秒事件序列 trace）；
  复现循环多形态 ~600 runs 捕获 2 次失败。
- **证据链**：失败特征（60s 不退 + is_registered=0 + AGENT::0001×2）+
  健康 trace 亚秒 → 代码穷举定位结构洞：replay_deferred_register 转正常
  注册时 RegisterAck 送达无保障（send 失败仅 WARN）+ worker 注册协议
  无 ack 重发（"不假设时限"被实现为无限静默等待）+ deferred 条目
  replay 时已清（deadline 兜底失效）→ ack 丢失即永久挂死。
- **修复**：注册守望线程（register_watchdog_loop）——事件驱动 ack 等待
  + 超时指数退避重发（500ms ×2 上限 30s，worker_register_ack_retry_
  initial_ms 键）。职责分层：连接级丢失由既有 on_disconnect→
  reconnect_loop 事件驱动恢复（毫秒级）；守望仅覆盖 master 活着但
  应用层吞消息场景。cv 持锁 notify（注册成功即刻退出零空转，
  initiate_shutdown 同步唤醒）。
- **回归**：新增 RegisterAckLossRecoveredByResend——master 侧
  drop_next_register_for_testing_ hook 确定性构造"首条注册被吞"，
  修复前永久挂死、修复后重发恢复；agent_network_test 切 test_hooks 库。
- 验证：全量单测 59/59 + QA 147/147；修复后复现循环（12 核超额，2×125 runs）全部通过，原失败形态消失。
- 教训记录：唯一失败现场曾因清理命令（rm r4_round_*）丢失——现场文件
  在根因确认前不可清理。

---
---

## 2026-08-17 (1): 矩阵数据入库（write_object）+ P3-23 复现调查进行中

**矩阵入库改造（用户裁定：数据不落共享文件，走框架的分布式数据与依赖管理）**：

- `solve_ras_graph` / `solve_ras_graph_v2` 的 `matrix_path` 参数改为
  `matrix_ref` 双模式：**对象模式**（DB 对象名，推荐——矩阵经
  `db.write_object(MATRIX_OBJ_KEY="__rasg__matrix", ...)` 入库，worker 经
  read_object 正常路径获取，写完才可见的框架语义彻底消除共享文件读写
  时序问题）+ 路径模式（.npz 文件路径，仅限 qa/scripts、big_qa 本地
  实验工具，不经分布式管理）。
- golden_solver：矩阵全程内存/对象流转（prebuilt 读入即与文件解耦；
  fallback 经进程私有 tempfile 生成后即删）；exact 解改内存计算
  （新增 compute_exact_from_matrix，无文件 IO）——P3-24 的重写竞争
  场景从数据流层面消灭（原子写修复保留为通用防御）。
- flows.py（SolverProject）solve kickoff：删除"读出矩阵→落盘 db 目录
  →传路径"的中转（同款反模式），改直接写本 db 对象。
- runqa 内全部矩阵消费者已切换对象模式（golden ×9、test_ras_graph、
  test_ras_graph_v2 ×2、solver_project）；QA 全量 147/147 零回归。

**P3-23（probe 超时）调查**：按"确定性证据才下结论"要求重开——静态
审计证实 deadline 兜底链完整（15s 登记 + 5s 心跳循环检查 + 保守拒绝，
ProbeAck 丢失最多 20s 延迟退出，无法解释 60s 挂死）；已建 6 分支预测
表（A-G 各有必现日志签名），定向复现进行中（bazel 版已复现 1 次
1/100，直接二进制循环保现场版运行中）。

---
---

## 2026-08-16 (8): 测试审计行动落地——5 项补覆盖 + G2 边界缺陷修复 + P3-24 根治

按 coverage-report-2026-08-16 行动建议执行（③④①②⑤⑦ 依依赖排序）：

- **③ 游离测试处置**（commit 见历史）：requires_parsing/read_cache 注册进 BUILD，
  删 6 个与 C++ 重复的游离文件
- **④ 冗余清理**（commit 见历史）：backup 三胞胎重建 + 孤儿接回、freeze R2、
  solver 参数矩阵 14→1、单测 R1-R5；QA 162→147 零覆盖损失
- **① auto_backup EWMA**（commit 见历史）：master hooks + 5 单测 + 全链路 QA
  e2e（suggest→EWMA→trigger→backup，容量隔离绕 ObjectCache 的约束已注释）
- **② 断连宽限 QA + 缺陷修复**（commit 见历史）：qa/fault/grace case；发现并
  修复 G2 边界遗漏——宽限期 IDLE worker 仍被派新 task（WorkerInfo::in_grace_
  四处接线 + worker_manager 2 单测）
- **⑤ PeerRpcServer 单测**（本 commit）：7 用例覆盖 listen/端到端往返/延迟
  响应/失败通知/BYE 优雅关闭（正常关闭不 fire disconnect 的语义已校准）/
  connect 重试/stop 清理——v2 daemon 通信底座告别零单测
- **⑦ P3-24 根治**（本 commit）：根因=compute_exact_solution 后台线程原地
  np.savez 重写共享 npz 的 truncate 窗口（高负载下 worker 首读撞窗 EOFError）；
  修复=三处写协议统一原子替换（savez 到文件对象 + os.replace；np.savez 对
  非 .npz 后缀路径自动加后缀的坑已绕）；ras_graph_io_test 回归防护；
  coverage 单跑×2 + coverage 全量 -j2 147/147 验证。P3-24 置 FIXED

**会话被杀事件定论**（2026-08-16 23:49）：非 guest OOM——宿主 Windows commit
耗尽（Event 2004：vmmemWSL 7.5GB + ZCode 1.1GB + Edge 0.9GB，commit 29.8GB
vs 上限 29.6GB）→ WSL VM 整体重启；连带 fly 二进制写坏（0 字节，已重建）。
measure_coverage.sh Python QA 阶段降 -j2（实测 guest 谷值 4.1GB）。

---
---

## 2026-08-16 (7): 测试审计 + 覆盖率实测（85.8% C++ / 79% Python）

全面审计 56 个单测 target（~1050 gtest + 111 py 用例）与 162 个 QA case，
按 docs/coverage-testing.md 方法实测最新覆盖率。产出
[coverage-report-2026-08-16.md](coverage-report-2026-08-16.md)（覆盖矩阵/
冗余清单/缺口排序/行动建议）。核心发现：

- **覆盖率**：C++ lines 85.8%（8773/10224，vs 7 月 85.3% 基线，代码基数
  缩小后持平略升）/ functions 78.4%；Python 79%（2625 stmts）。模块序：
  common 97.9 > serialization 96.5 > task 96.0 > log/core ~90 >
  storage 88.6 > agent 83.4 > network 81.3 > main 67.0。
- **双侧零覆盖缺口**：auto_backup master 侧 EWMA 判定（无测试且无
  for_testing hook）；QA 层断连宽限重连（reconnect_timeout 全 =0）与
  probe 拒绝、压缩特性专项断言。
- **单测缺口**：PeerRpcServer/PeerChannelGroup 零单测（仅 QA 兜底）、
  bandwidth_probe_loop、on_task_assign 预取回填。
- **游离测试**：8 个 Python 测试不在任何 BUILD（含 requires 解析唯一
  测试 test_requires_parsing.py 18 例脱管 = 该特性无 CI 覆盖）。
- **冗余**：QA backup 三胞胎（3 case 跑同一 md5 脚本且名不副实）+
  孤儿 run1/run2；freeze 拒绝家族 ×5；solver 参数矩阵 14 文件；
  单测 compressing_streambuf 双份、write_registration 重叠等。
- 工具修复：measure_coverage.sh 单测 timeout 60→300（master_agent_test
  90 用例 118s 曾被截断）+ stale target 过滤（io_thread_pool_test 残留
  二进制）+ QA 失败不阻断报告生成。
- ISSUES 新增 P3-24：n500 case 在 coverage 全量 -j6 下 npz EOFError
  2/2 复现、单跑与串行全过（写读时序窗口待专项）。
- 顺带修文档死引用：network/module.md 的 IOThreadPool 残留、
  test/module.md 的 test_object_test 不存在 target。

---
---

## 2026-08-16 (6): 文档全量重组——删 20 项 + 三组整合 + 文档地图

### 删除（任务完结、结论已沉淀到活跃文档，git 历史可查）

- **superpowers/ 整目录**（2026-05/06 过程稿 5 份：db-meta 设计→ADR 0001 已沉淀、
  dataservice 网络层/reactor 异步→architecture 已沉淀、last_respone.md 草稿）
- **locality 三部曲**（scheduling-plan/review/decoupling-fix-plan，6/28-30，
  任务全部完成，决策记录在 roadmap §三）
- **审计/快照 5 份**：ARCHITECTURE_REVIEW（6/12，结论入 ISSUES/roadmap）、
  code-audit-2026-08-08、redundancy-audit-report（7/2，已执行）、
  fifth-class-verification（7/3）、coverage-report-2026-07-31（方法论保留于
  coverage-testing.md）
- **旧性能分析 3 份**：read-write-optimization、zero-copy-analysis（结论沉淀于
  performance-analysis.md §0）、solver/performance.md（被 optimization-roadmap
  + perf-n1000 取代）、根目录 profiling_report.md
- **message-system-review.md**（7/30 评审记录，已执行）

### 整合

- **perf-baseline-dataservice-lock + perf-baseline-scheduling-hotloop →
  perf-baselines.md**（内容不变，src 两处注释 + roadmap/performance-analysis
  引用同步）
- **solver 预研 5 份 → solver/rejected-alternatives.md**（allreduce/GPU×2/
  分布式范式/PCG 路径，每篇保留结论与关键依据；optimization-roadmap 引用同步）
- **architecture/overview.md 并入 architecture.md 后删除**（agent 逐节对比
  迁移 12 项独有信息）：新增 §4.4 模块依赖关系、§5.0 任务生命周期、线程
  模型补停止方式与心跳周期（master 5s/worker 10s 硬编码）、部署图更新组件名
  （DataService/DependencyGraph/Reactor + ObjectCache + HeartbeatMonitor +
  storage_only）、§6.1 帧格式补 big-endian、§6.3 补 WRITE_REGISTER_ACK/
  TaskSubmitAck/PROBE/SPAWN 行并删已退役 IDX_REQUEST/IDX_RESPONSE、消息总数
  52→54、§七补 src/main/、技术栈补网络行、§4.3 补 headers 决策行

### 新增与修复

- **docs/README.md 文档地图**（按用途快速入口 + 全量索引 + 文档约定）；
  AGENTS.md 文档表补地图入口
- 修复全部活跃引用断链（CLAUDE.md/roadmap/performance-analysis/
  coverage-testing/sitecustomize.py docstring/architecture/message-system/
  remaining-todo/competitor-analysis 头注）；全仓残留扫描零断链

---
---

## 2026-08-16 (5): push 门禁配方用户裁定 + proc mem 改物理内存

- **两套测试严格串行**（用户确认）：unit test 全部通过后才启动 runqa，
  不做并行叠加（OOM 峰值构成的关键项是两套测试同时跑）。
- **配方**（用户裁定）：unittest `bazel test --jobs=4`、QA `runqa -j6`
  （runqa 默认并行从按核自适应 nproc-2 改为固定 6，低核小机可显式 -j 覆盖）。
  实测：单测 56/56、QA 162/162（70s，avail 谷值 3.4GB）。
- **hook 增强日志**：各阶段前后打印 `[mem] xxxxMB available` 内存水位，
  OOM 复发时有现场可查。
- **`proc mem` 改物理内存**（system_info.cpp）：VmPeak（虚拟地址空间峰值，
  多线程 C++ 常态上 GB、与物理占用无关，OOM 排查中被误读）→ VmRSS +
  peak VmHWM（均为物理值）。

---
---

## 2026-08-16 (4): Solver 全家逐项复核（1 清理 + 1 有意保留 + 4 待触发/不做）

按「先核实文档描述与代码一致、是否真需做」流程复核 remaining-todo §四
全部 6 项（对照 docs/solver/optimization-roadmap.md 2026-08-04 决策）：

- **增量 residual**：删除 `ras_graph_daemon.py` 的 `residual_cached` 空壳
  （声明后从未赋值/读取）。现行设计为每步全量精确计算 r = b - A·x
  （代码注释明确「保证数值正确性」），实现增量缓存反而违背设计决策。
- **v1 task 链**：改判「有意保留」——v1（solve_ras_graph）是 QA golden
  正确性基准（golden_solver/test_ras_graph/verify_2d_partition/
  bench_omega_sweep）与 big_qa scaling 对照链；v2（solve_ras_graph_v2
  常驻 daemon + PeerChannelGroup RPC 直连，iter-refactor 特性 1+2 已实施）
  是性能主链。双链分工明确非冗余，原「待清理」定性有误。
- **PCG / master reduce RPC / Worker 进程复用**：⏸ 待触发——optimization-
  roadmap 已裁定：PCG「可行但有硬瓶颈」（无轻量 Allreduce，通信占 93%）；
  reduce 原语已被方向 2（迭代重构，已实施）取代；进程复用「风险/收益比
  不划算」（初始化大头 BFS 633ms + LDLT 1171ms 算法固有）。
- **树形归约**：改判 ⛔ 明确不做——optimization-roadmap §五裁定 fly 树形
  是「伪 O(log nsd)」（配对常数项差 MPI 100x）；原「nsd≥16 再做」的清单
  描述过期。
- 附带修正：iter-refactor-impl-plan.md 步骤 1.2「（或复用 IOThreadPool）」
  措辞（该类已于本日死代码批次删除）。

验证：全量单测 + QA（见 commit）。

---
---

## 2026-08-16 (3): 死代码清理批次（逐项核实后删除）

按「先核实是否真死、再删」流程执行 remaining-todo §六 全清单。核实结论
先行：BE32 早已统一（7 月，清单未更新）；removed_objects_ 非死代码
（freeze 报告活跃，原清单误判）；其余 8 项确认死，全链删除：

- **IOThreadPool**：io_thread_pool.{h,cpp} + EXNetIOThreadPool export +
  network/py re-export + io_thread_pool_test + network_test.py 用例类。
  C++ 生产零使用（Reactor/HandlerThreadPool 已取代其设计角色）。
- **旧 auto_backup 判定全链**：DataService::decay_remote_access /
  decay_after_backup / evaluate_auto_backup + BackupDecision struct +
  4 个测试用例 + 死配置键 backup_threshold/backup_replicas/
  backup_decay_interval/backup_decay_factor（含 docs/core/module.md 同步
  与 master_agent_test 死 set 行）。双层重设计（worker suggest + master
  EWMA）已全链取代；核实发现 evaluate_auto_backup 也仅测试引用。
- **temp_objects_ + Database::mark_temp**：字段零读取（is_temp 权威源在
  local_idx_），export + database.py `_write_temp` 调用行一并删除。
- **LocalObjectInfo.error_message_**：写入后条目立即 erase 无人读；删除
  字段，on_write_failed 的 reason 参数改 DBG 日志输出（13 处调用方签名
  不变，诊断价值保留）。
- **IDX_REQUEST/IDX_RESPONSE 死枚举**：零引用删除，15/16 空号留注释
  （显式赋值，不改既有 wire 值）。
- **Solver 死 export**：ex_slv_vec_* 7 个（GMRES 向量算子）+
  ex_slv_extract_subdomain_matrix_oras + C++ 本体 extract_subdomain_matrix_oras
  （Python/qa/big_qa 零引用）；std_to_vec/vec_to_std helper 与
  ex_slv_graph_expand_overlap 活跃保留。

ISSUES 新增 P3-23：agent_network_test DuplicateWorkerRegisterRejectedAfterProbe
高并行负载偶发超限（本批次全量首跑复现 1 次，单独 4/4 + 全量重跑过，
与所删符号零因果，记录待专项）。

验证：全量单测 56/56（较此前 -1：io_thread_pool_test 随类删除）、
QA 162/162、全仓残留引用 grep 零命中。

---
---

## 2026-08-16 (2): S4 复核关闭（非缺陷）

对 roadmap [S4]「TIER1 FAILED 无差别回退 TIER2」做修复前核实，结论**不修**
（用户流程要求：每项修复前先确认文档描述与代码一致、是否真需要修）：

- **FAILED 态对读者不可见**：`on_write_failed`（data_service.cpp）在同一
  unique_lock 内 store FAILED 后立即 erase 条目，diag=2 的 FAILED 分支竞争
  窗口为零；wait 唤醒的读者重查为 not_found。
- **读旧副本是正确语义**：对象曾 backup 时本地写失败从 TIER2 读副本，
  provenance hash 保证幂等重算内容一致；按原方向「FAILED 直接失败」反而
  拒绝读有效副本，破坏正确性。
- **无副本场景闭环**：TIER2 秒空 → TIER3 master 无位置 → `can_still_produce`
  驱动 wait_obj 收敛。
- 既有测试锁定该行为（data_service_test.cpp:1424「FAILED object should
  return false (fallback to TIER2)」）。

roadmap S4 → ✅ 关闭；remaining-todo §二 S4 行同步。遗留
`LocalObjectInfo.error_message_` 死字段（写入后条目立即 erase、无人读）
归死代码清理清单。

---
---

## 2026-08-16 (1): 补记 8/15-8/16 七个 commit + 全量文档状态同步

### 补记：存储面 H 系列（2026-08-15 下午，4 commit）

用户确认语义的存储面四机制，代码与 architecture.md 已同步，此处补记
DOC_CHANGELOG 条目：

- **H1**（2020124）：`select_backup_worker` 三级 key——host-disjoint 故障域
  隔离 > storage_only 优先 > 名下副本字节最轻（`get_worker_bytes_batch`
  一次锁内聚合磁盘水位）；host 全冲突 best-effort 回退层内同序。
- **H2**（5dfc5e3）：`lookup_all_remote_idx` 返回前统一排序——存活
  storage_only > 存活 hybrid > 已死 holder 排尾（registry 维护 alive_）；
  role 协议传播（DataLocation.storage_only 字段 + TIER3 回填）。
- **H3**（fbb9bc4）：判死后同 host storage_only 只读接管读服务
  （`try_storage_takeover` 复用 IdxLoad 链路 + `rebuild_remote_idx_for_worker`
  追加 holder）；全灭 fail 延迟 60s；restore 等价副本跳过 + ObjectCache 失效；
  修复预取单副本 front() 选中死 holder 卡 30s 的缺陷（改 lookup_all 排序首选）。
- **H4**（ee190da）：master 自动补齐存储节点（`auto_storage_nodes_enabled`
  默认关；posix_spawn /proc/self/exe + SETSID + fd 零继承——fd 继承致连接
  ESTAB 残留卡死是实测根因）+ 宽限超时判死提醒 AGENT::0006 + 重复注册
  先到先得防护（WORKER_PROBE 活性探测 + deferred 注册重放）。

### 补记：8/16 三个 commit

- **5651b09**：50 轮稳定性测试暴露的四个并发缺陷（未注册窗口 WriteRegister
  静默丢弃 / 首连窗口缓冲上报无人 flush / do_write_register 可见性顺序 /
  重复注册时序假设），50/50 全过。
- **cbbb3fc**：注册时序语义收口——写注册 pending 阻塞（终态驱动，替代 5s
  超时）+ RegisterAck 先于调度可见（TCP 同连接保序，assign 抢跑窗口根除）。
- **19d9afb**：断连消息语义统一——`pending_master_sends_` FIFO 重放；A 类
  同步 RPC（DbPath/Freeze/Var/Remove）挂起重放拿 Ack 才放行；B 类入队重放
  （Backup/能力增删/ObjectRemoved）；TaskSubmit 升级 Ack 强语义
  （TASK_SUBMIT_ACK=57，request_id 匹配带回 task_id，断连丢子任务根除）。

### 本轮文档状态同步（过期修正）

全面对比文档 vs 代码/git 历史后的修正：

- **remaining-todo.md**：P1-8 ❌→✅（lambda 已返回 bool）；P3-19
  MetadataClient ❌→✅（mock e2e 已补）；auto_backup 🔄→✅（worker
  suggest + master EWMA 已全链落地）；`_MIGRATED_TO` 移出死代码清单（db_id
  废弃后已成正式迁移重定向机制）；§七不一致表全部处理；throw 残留 12→9
  （object_header.cpp 已清零）；新增 8/15-16 完成项表；**用户裁定移除两项**
  （WriteBackQueue 单 worker——设计约束非欠账；MessageHeader message_id_/
  timestamp_——二次检查确认保留：协议头标准槽位，删除改全量 wire format
  收益近零）。
- **roadmap.md**：S1-2 🟡→✅（per-object mutex/cv 已删 + atomic + per-db
  cv）；S3 补 ✅（2026-08-12 已修，commit 1bdf244）；S4 补 🟡 部分完成
  （INCOMPLETE 快路径已做，FAILED 仍无差别回退）。
- **ISSUES.md**：P3 编号去重——旧 P3-18（Dead code cleanup）→ **P3-21**、
  旧 P3-19（MetadataClient）→ **P3-22**（新 P3-18/P3-19 已被 commit 与
  CHANGELOG 引用保持不变）；P3-18/P3-19 条目 Next 残留（已修复项仍写
  "专项排查"）清理；Summary 表 8/12→8/16 更新（P3 6 项 5 fixed）。
- **architecture.md**：「尚未实现」列表修正——移除 Locality（早已实现默认
  开启）、Worker 失败恢复（断连宽限 + 重入队已实现）、Worker role（已实现
  却列在未实现下）；页脚日期 2026-06-17→2026-08-16。

---
---

## 2026-08-15 (3): Logger 自动 flush（累计字节数 / 时间间隔）

用户确认增强：DEBUG/INFO 累计写入达 log_flush_threshold_bytes（默认 64KB）或距
上次 flush 超 log_flush_interval_ms（默认 1s）时自动 flush——避免日志文件更新延迟
过长（P3-19 根因：此前仅 WARN/ERROR 立即 flush，DEBUG/INFO 完全依赖退出 flush，
测试运行中读日志必漏行）。写时惰性判定（无后台线程——规避 P3-18 同族退出期线程
问题）。参数经 Logger::set_flush_params 由 main.cpp 从 config 注入（fly_log 不依赖
Config：cc_shared_library 禁止 fly_log_so/fly_core_so 同时静态链 fly_core）。
P3-19 置 FIXED。

## 2026-08-15 (2): F3 worker role 落地（hybrid/storage_only，静态身份，调度不感知）

用户确认语义：role 是独立于 attributes（可变、参与调度匹配）的**静态身份**——
注册时设定、不可变更（无修改途径，storage_only 因此不可取消）；取值仅
hybrid（默认）/storage_only；**调度决策不感知 storage_only**（idle 候选层过滤，
scheduler 零 role 概念）；storage_only 仍参与心跳判死/数据面/internal 数据
task（merge/backup 搬运）与 backup 目标。

- **H1**（a66716c）：WorkerRole enum + WorkerInfo.role_ + RegisterMessage.role_；
  get_idle_workers/count 候选层过滤（TaskScheduler 零改动——select_best_worker
  全分支被 idle 集合门控）；worker/master 注册链透传（宽限重连同值覆盖）；
  export 构造重载。单测三个新用例。
- **H2**（本 commit）：launch config role key → CLI --worker-role →
  ProcessInfo.worker_role → runtime → Worker(role=)；print_usage 补
  --worker-attributes（此前漏列）与 --worker-role；QA test_worker_role
  （storage_only 不入候选、无计算 task 痕迹、注册上报验证）；
  文档全面修正（architecture.md 虚构 CLI/role 语义/"尚未实现"、
  fly/__init__ docstring、remaining-todo F3 ✅、roadmap F3 落地说明与
  实现方案差异、wait_for_all_workers 只数可调度 worker）。
- 调试记录：QA 断言 worker 日志需在 stop 后读（INFO 缓冲退出才 flush——
  P3-19 同机制，顺手为 P3-19 补充了根因证据）。

---
---

## 2026-08-15 (1): worker 断连重连 + master 宽限（G 系列 4 commit）

用户多轮收敛的最终语义：断连仅指网络闪断（master 挂=全群失败）；首次注册默认
不假设任何超时；断连后两侧对等的 2min 宽限内 task 存活、worker 指数退避重连、
重连后正确上报；master 权威 remote_idx 永不因读失败踢出；数据全灭快速失败。

- **G1**（96d148f）：`worker_register_timeout` 默认 300→0（首注册不假设超时，
  撤回早期"统一保活"方案）；新键 `worker_reconnect_timeout`（默认 120）。
- **G2**（97c19b8）：master 断连宽限（grace_deadlines_，task 存活 RUNNING、
  宽限内豁免心跳判死、重连注册保留 BUSY/task 关联、迟到上报 assigned 校验）；
  权威 remote_idx 读失败保护（remove_remote_location 按 ProcessInfo 进程角色
  豁免 master）；数据全灭快速失败（判死时全 holder 失效对象 → mark_data_removed
  + 等待依赖 task 直接 fail，AGENT::0003；含多副本全灭，宽限中 holder 不算失效）。
- **G3**（106848c）：worker 断连指数退避重连（reconnect_loop 常驻至 ack 确认或
  宽限耗尽；连环闪断同线程处理）+ task 上报缓冲（pending_reports_，RegisterAck
  后 flush）；master_conn_ 改 atomic；reconnect_timeout=0 逃生口保持断连即死。
- **G4**（本 commit）：文档（core/module.md 两键最终语义、architecture.md
  §3.4 两阶段生命周期+权威保护+全灭快速失败）。

测试：G2 五用例（宽限保 task/超时恢复+全灭单副本失败/多副本全灭/重连保留/
master 读失败保护）+ G3 三用例（闪断模拟 hook 存活+缓冲+送达/宽限耗尽退出/
逃生口）；调试记录（临时对象迭代器对 SIGSEGV、master.stop 广播 Shutdown 不宜
模拟闪断）；全量单测 57/57、全量 QA 156/156。

---
---

## 2026-08-14 (4): 17-commit 批次——merge 失败全链路 + connect 指数退避 + Config 读写锁 + 锁内 IO 拆除 + QA 等待批量改造

### A. Config 读写锁（#5，a9d5aea）
`std::mutex` → `std::shared_mutex`（高频 get_* 共享、低频 set_*/reset 独占）；删除全仓零调用方且无法持锁保护的 `all_ints()/all_strs()`；新增 SaveToFileConcurrentWithSetIsSafe 并发用例。

### B. worker connect 指数退避 + 两侧统一保活（cf3bb9c, 7f4c476）
`worker_register_timeout` 默认 0→300（5min，一个键控制两侧：master 占位符保活 + worker connect 重试总窗口 + wait_workers_registered 默认超时）。`WorkerAgent::start` connect 失败按 `worker_connect_retry_initial_ms`（500ms）×2（上限 10s）退避重试。领域约束：master 挂=全群失败，仅覆盖瞬时抖动与短时过载。

### C. merge_db 失败全链路（6835625..049115b，6 commit）
- C1 正确性 bug：execute_merge_object 写盘失败（write_record_checked/flush_checked/get_last_entry）走 TaskFailed，根除假成功（原 remote_idx 指向无数据对象+误判 ok 删源）。
- C2：send_merge_task 未连接路径回滚 BUSY 槽（cancel_task_if_assigned 精确匹配）；internal task 不再以空 submission 污染 failed_tasks.bin。
- C3 失败清理协议：MergeTaskState.db_path_ 按 db 精确清理（修跨 db 误用）；MergeCleanupMessage.purge_target_（源全保留，持有 merge writer 的 worker 自判删产物）；Python ok=False → cleanup + raise RuntimeError；merge_db 新参数 task_timeout。
- C4 QA test_merge_fail_then_remerge（失败可见/源保留/产物清理/重 merge 全闭环）+ write_object 保存等级 `cache="none"`（仅落盘不进 low 缓存，数据搬运场景 + 注入前提）。
- C5 空清单防误删源：idx 文件存在但 0 条目（损坏被误当真空）→ RuntimeError 拒绝（原 0 task 全"成功"→删源丢数据）。
- C6 删源失败自动重试一轮；仍失败发 STOR::0004（ERROR，含残留 worker 清单）提醒手动删除。

### D. Database 自保护 + 锁内 IO 全量拆除（8262fef, a1c210f）
- D1：Database state_mutex_（路径成员/writer_ 操作/removed_/temp_/freeze check-and-set）；修两个前置 bug（on_master_register_write 递归 shared_mutex 死锁隐患、worker register_write_with_master 持读锁 5s 等待）。
- D2-D4：db_instances_/databases_ 约 30 访问点临界区收敛为"find+拷 shared_ptr"，freeze/var/rebuild/_DB_META append/send/drain 全部出锁；写点锁外构造+二次检查插入。

### E. QA 假设型等待批量改造（6f1028a，72 处）
launch 后手写 for-sleep 38 处 + wait_for(worker_count) 34 处 → wait_workers_registered(timeout=60)。

### F. 文档
core/module.md（worker_register_timeout=300 两侧统一语义 + worker_connect_retry_initial_ms）；architecture.md §3.4（保活/重试语义）。

---
---

## 2026-08-14 (3): worker 唤起占位符 + 注册等待专用 API（不假设注册时限）

### A. 背景
bsub（LSF）调度场景：master 唤起 worker 的请求发出后，worker 可能分钟级才真正启动注册。原代码多处假设"worker 会在 N 秒内连接"（`wait_for_all_workers` 默认 30s、`load_db`/`merge_db` 硬编码 30s、QA `wait_for(worker_count, timeout=10)`），CPU 饱和压力测试实测误报。

### B. 机制（8bd1ce6 + c545d7c + c76a967）
- C++ MasterAgent 唤起占位符 `expected_worker_ids_`（ConcurrentUnorderedMap，worker_id → spawn 时间戳）：`expect_worker` 登记（重复=刷新时间戳）、`on_worker_register` 转正、heartbeat 检查线程按 config 清理超时项。占位符不参与调度/不进连接表——stop drain、心跳、调度行为零变化。
- Config 新键 `worker_register_timeout`（默认 0=不假设时限；>0=超时清理占位符并作为等待 API 默认超时）。
- Python：`_spawn_process_worker` Popen 前登记（顺序关键：防注册快于登记导致转正落空）；`Master.wait_workers_registered(timeout=None)`（无限等时每 30s 打 INFO 进度）；`load_db`/`merge_db` 三处硬编码 30s 收敛为 `_wait_spawned_workers()`。
- 顶层导出 `fly.wait_workers_registered` / `fly.expect_workers`（外部唤起场景手动登记）。

### C. QA
- 新 case `test_wait_workers_registered`（正常注册 True / config 2s 幻影超时 False / 显式 timeout 优先）。
- 脆弱等待改造（压力实测失败实例）：mapreduce 10 处 `wait_for(worker_count>=N, timeout=10)`、mixed_fail_run1/2 手写 20s 循环、project 3 处 `wait_for_workers(1)` → 新 API；temp_zero_copy subcase timeout 60→180（纯容量型）。

### D. 文档
- `docs/core/module.md` config 表：`worker_register_timeout` 键。
- `docs/architecture.md` §3.4：等待 worker 注册章节（含 bsub 外部唤起用例）。

---
---

## 2026-08-14 (2): 锁使用收敛与封装改造（7 commit 系列）

### A. P0 — on_var_ack lost wakeup 修复（真实 bug，确定性复现）
- **根因**：`on_var_ack` 用 `take_for_complete` 取出 pending 后锁外写非 atomic 字段 + 调不持锁的 `PendingRpcMap::notify_all()`——cv lost wakeup（waiter 持锁查 pred 与进入 wait 之间的窗口里 notify 落空 → `get_var_sync`/`set_var_sync` 卡满 5s 超时）+ data race，与 8419526（DataServer::stop）同族。
- **修复**：改持锁 `complete()`（FlyBuffer 构造为纯内存操作）；删除 `take_for_complete`/`notify_all`/`notify_one` 无锁接口（无调用者）；`worker_agent` 其余 4 处锁外 notify（`on_task_assign`、`initiate_shutdown` 三个 cv、peer_rpc 两个 handler）一并挪入锁内。
- **确定性测试**：`PendingRpcMap` 加 `FLY_ENABLE_TEST_HOOKS` 专用 `pre_sleep_hook_`（wait_for 循环 pred==false 后、入 wait 前触发）+ `std::latch` 钉死窗口，red 用例复现（elapsed=1000ms 超时），修复后转 `CompleteDuringPredicateWindowWakesWaiter` 永久防回归。

### B. 封装扩展（ConcurrentMap v2 / PendingRpcMap v2 / ConcurrentUnorderedSet）
- `ConcurrentMapBase`：`update(key,f)`（锁内 RMW，miss 默认插入）、`take`/`take_any`（原子消费式读取）、`with_lock(f)`（持锁逃生口）、`get_or_insert` 内部 move 化；新增 `ConcurrentUnorderedSet`（`insert` 返回是否新插入）。首个单测 `concurrent_map_test`（此前零覆盖，含并发守恒用例）。
- `PendingRpcMap`：`insert_if_absent`（Problem5 防重置）、`wait_for(..., erase_on_timeout)`（true=worker 侧超时即 erase；false=merge 侧条目跨 wait 生命周期）、`with_lock(f)`；`mutex_` mutable + `find` const 化。单测扩至 12 用例。

### C. 迁移（删除 11 个裸 mutex 声明）
- **P2（4 个单容器+单锁）**：`recorded_workers_`→ConcurrentUnorderedSet（append 副作用锁外恰好一次 + 消除嵌套锁）、`backup_scores_`→update（EWMA RMW；find 改不再隐式插入空条目）、`merge_writers_`→get_or_insert（unique_ptr→shared_ptr）、`task_dependency_locations_`→update/take（顺带把 submit 时的 `lookup_remote_idx` 预取挪出锁，原临界区含 DataService 读锁重活）。
- **P1（master 4 套手写 map+mutex+cv 收敛 3 套）**：`pending_delete_acks_`/`pending_merge_cleanups_`/`merge_task_states_` → `PendingRpcMap`。语义冲突点逐一保真：Problem5 insert-if-absent（回归测试盯防）、wait 超时保留（cleanup_after_merge 统一消费）、持锁遍历/条件批量 erase（with_lock）、未连接失败路径的无条件 upsert 语义保持。有意偏差（已记录）：merge_cleanup 条件 notify→无条件 notify_all（空唤醒无害）、backup_scores find 不再残留空条目。`msg_count` 第 4 套（单值+旁挂 vector 非 map）有意保留手写。
- 每套迁移以 characterization 测试先行（`RecordWorkerInfoAppendsMetaOncePerTuple`）+ Problem5/MergeObjectEndToEnd 回归铁门槛。

### D. 文档
- `DEVELOPMENT_GUIDELINES.md` 新增 **§13 并发与锁规范**：封装优先级表（ConcurrentMap > PendingRpcMap > 类级封装 > 裸 mutex）、"cv notify 必须持锁"铁律（两次事故案例 + 窗口机理 + 确定性测试模式）、锁内禁 IO（标注 db_instances_/databases_ 历史欠账为待专项）、并发测试写法（latch 确定性优先 / 禁 sleep-assert / 无屏障并发测试在高负载下会被串行化误报——data_client_pool_test 实例）。
- `CLAUDE.md` §必须遵循 加第 6 条（并发封装优先 + notify 持锁 + 指针）；`AGENTS.md` Key Constraints 同步一行。

### E. 顺带修复
- `data_client_pool_test` 并发用例加 latch 屏障：无屏障时 pre-push 高负载下 4 线程被 OS 完全串行化，首请求归还 fd 后其余复用 → connect_count=1 误报（本次 push 实际触发）。


## 2026-08-14: HandlerThreadPool lane 并行分发 + SIGTERM 优雅退出接入

### A. handler 并行分发（原 ARCHITECTURE_REVIEW §3.1 P1 项）
- **机制**：`Reactor` 按 `handler_lanes`（Config，默认 4，0=内联）创建专用串行 lane。帧提取（recv_buffers_ 推进）留在 reactor 线程，decode+handler 投递到 `conn_id % lanes` 的 lane：同连接消息严格 FIFO 保序，跨连接并行。connect/disconnect/error 回调同经该 conn 的 lane，保证与在途消息先后关系。lane 队列无界（控制面消息不可丢），shutdown/`drain_handlers` 排空语义。
- **前置并发修复**（并行化必要条件，其中 3 处为存量竞争）：
  1. master `db_instances_` / worker `databases_` 加 `std::shared_mutex`（reactor/P/lane 线程 vs Python 线程注册/merge 改路径）
  2. failed_tasks.bin append/读改写互斥（跨 lane handler 与 attr-tick/watchdog 线程）
  3. `do_write_register` frozen 检查与 provenance 登记合并同一临界区（TOCTOU）
  4. per-conn send mutex 改 `shared_ptr` 持有——disconnect erase 与并发 send 的「mutex 被持有时析构」use-after-free（存量隐患，MergeObjectEndToEnd 1/3 概率堆损坏实测复现，修复后 40/40 稳定）
- **修复的接入期 bug**：lane 线程先于 `lanes_` vector 填充完成启动的悬空竞态；`request_db_path` 持读锁等待响应的自死锁；`reactor_.reset()` 先置空成员再析构导致迟到 handler 解引用空指针（新增 `Reactor::drain_handlers`，所有 reset 前调用）；`restart_failed_tasks` 持 failed_tasks 文件锁重提交 → `schedule_tasks → persist_failed_task` 再取同锁的自死锁（QA graceful_shutdown 等 6 case 实测挂死，gdb 查 mutex `__owner` 确认主线程双重持锁；修复为文件锁只覆盖读取+删除）。
  5. `DependencyGraph::get_task_requirements` 返回引用在锁释放后悬空——与 `set_task_locality_hint`（move 赋值 locality_hint_）/`remove_task`（erase 节点）并发即 use-after-free（QA golden_n50_sd9 compute_scores 段错误实测；改按值返回锁内快照）。
- 文档：`network/module.md`（Reactor 章节 + HandlerThreadPool 行）、`core/module.md`（handler_lanes 键）、`architecture.md`（线程模型 + 决策表）、`ARCHITECTURE_REVIEW.md` §3.1 标已解决。

### C. master 重启端口策略修复（50 轮稳定性测试第 42 轮定位）
- 50 轮全量 QA（-j4，每轮 153 case）第 42 轮 `test_locality_perf` 失败：`Failed to create listen socket`。
- 根因（存量设计问题，与 lane 无关）：`MasterAgent::start()` 重启时复用上次绑定端口（首轮 ephemeral 绑定后 `port_` 被覆盖）——close→rebind 窗口内该端口可被并发进程抢作临时源端口，SO_REUSEADDR 对活跃连接无效（42 轮 × 每轮 6 次进程内重启 ≈ 252 次命中 1 次；单独复现 15/15 不触发）。
- 修复：新增 `listen_port_`（构造请求端口），start() 每次按它 bind——port 0 = 每次拿全新临时端口，固定端口仍尊重用户意图。单测 `RestartUsesRequestedPortNotLastBound` 覆盖。
- 修复后剩余 9 轮全绿，50 轮总计 41+9 全部通过。

### B. SIGTERM 优雅退出（原 code-audit §2.1 死代码项）
- 新增 `agent/cpp/graceful_shutdown.{h,cpp}`：进程级信号灯（`sigaction` + SA_RESTART，handler 内仅 atomic 写）。`main.cpp` 启动即注册（覆盖 Python 起动前窗口）。
- master：heartbeat 检查线程（≤5s）发现信号灯 → `trigger_graceful_shutdown()`（幂等）在独立线程执行**完整 `stop()` 三阶段 drain**（等 RUNNING task → message summary → Shutdown 广播 → persist pending）。
- worker：`is_running()` 观察信号灯，Python poll 循环（100ms）退出后走 `agent.stop()`。`main.py` 的 Python SIGTERM handler 首行同步置 C++ 信号灯（覆盖主线程阻塞在 C 调用、Python handler 无法执行的场景）。
- 删除死代码：`check_shutdown_request`/`sigterm_handler`/`drain_thread_`/`fatal_error_`（原实现无 drain 等待、drain 线程不 join，直接接入语义是错的）。
- 测试：`GracefulShutdownTest`（信号灯 set/reset/真实信号 / trigger → 完整 stop drain）；export `ex_agent_set_graceful_shutdown`。

---
---

## 2026-08-14: 补记最近 commit 的文档同步（keep-alive 连接池 + auto_backup 双层重设计）

> 以下两个 commit 此前未同步文档，本次补齐。

### commit a408523 — DataClientPool keep-alive 连接池
- `docs/network/module.md` DataClientPool 章节：重写为 keep-alive fd 复用语义（同 peer 多 fd、2×pool_size 容量模型、反倾斜+LRU 淘汰、三重健康保护、锁纪律）。
- `docs/architecture.md` / `docs/architecture/overview.md` 架构图：Layer 2 描述改为 "keep-alive 连接池 + 并发限制"。
- `docs/solver/allreduce-log-nsd-feasibility.md`："DataClientPool 是短连接" 标记过期，注明已落地 keep-alive（该文档 §103 的传输层优化建议已实现）。
- `docs/ARCHITECTURE_REVIEW.md` §3.11：标记已解决（借出 SO_ERROR 预检 + 失败即关）。
- `docs/performance-analysis.md`："短连接" 瓶颈项标记已消除。

### commit 6791c7f + fd24481 — auto_backup 双层重设计（worker suggest + master EWMA 聚合）
- `docs/architecture.md` §5.4 自动备份：重写为双层机制（worker `maybe_suggest_backup` 增量上报 + master EWMA 聚合 `score=cumulative/replicas` 判定、host 级分散选择、副本上限 + 大文件例外）；配置表替换为新 11 键；旧 4 键标注已废弃（新路径不消费，旧 evaluate/decay API 无生产调用方）。
- `docs/architecture.md` §6.3 消息表：修正备份消息行（`BackupTaskMessage` 不存在 → `BackupRequestMessage`），新增 `WorkerBackupSuggestMessage`（=52）。
- `docs/core/module.md` 配置表：backup 段同上对齐。

### 其他同期 commit（文档补齐 2026-08-14 第二轮）
- 8419526（DataServer::stop lost wakeup）、6c82ec9（Config 线程安全）、684eb8e（bazel standalone）初判"无需更新"，复查发现 5 处缺口，本轮补齐：
  - `docs/storage/module.md`：DataServer 新增「stop() 与 lost wakeup 唤醒纪律」子节（notify 持 send_mutex_ 的机理，cv 通用纪律）。
  - `docs/core/module.md`：设计决策表「无需锁」改为 mutex_/by-value 三行（原表述与实现矛盾）。
  - `docs/ARCHITECTURE_REVIEW.md` §3.10：重试无退避标已解决（TIER2 重构，旧文件行号失效）。
  - `fly.sh` guard_init_py 注释：根因更正为「sandbox hardlink + bazel runfiles O_TRUNC」（strace 定位），test 路径已 standalone 根治、build 路径保留自愈。
  - `docs/remaining-todo.md`：网络层 7 项状态翻转（HandlerThreadPool/send 非阻塞/退避/DCP fd/Config 锁/conn_send 锁 + 新增 lost wakeup、standalone 两项）。

### 全量文档一致性审阅（同日）
对照实现逐节核对活跃 module 文档并修复：
- `docs/network/module.md`：删除不存在的 `DataClient` 类章节与组件表条目（已被 DataClientPool 取代）；Reactor 事件循环移除不存在的 `io_pool_->process_completions()`（Reactor 不持有 IOThreadPool）；send 线程安全改为 per-conn mutex（非内核 send 原子性）；epoll 改"水平触发 + ONESHOT"（无 EPOLLET）；组件表补 TcpConnectionManager / HandlerThreadPool；设计决策表去掉 UDP/RDMA 表述。
- `docs/core/module.md`：配置表移除 8 个 ProcessInfo 字段（worker_mode/worker_id/master_port/master_host/data_server_host/script_path/interactive/cli_master_port 非 Config 键）、删除不存在的 `large_file_threshold`、`data_server_threads` 默认值 1→4、补 5 个缺失键（locality_scheduling_enabled/read_cache_size/temp_store_size/data_client_pool_size/solver_openmp_threads）；类声明对齐实现（`CMSharedPtr<Config>& instance()`、`get_str` 按值、`save_to_file/load_from_file`、mutex 成员）。
- `docs/architecture.md`：CLI 参数表重写（删 `--worker_mode`/`--master`/`--role`，补 `--worker`/`--worker-id`/`--master-host`/`--master-port`/`--log-dir`/`--worker-attributes`/`--config-file`）；启动示例改 `set_int`/`set_str`（kwargs 风格 `set()` 不存在）；§5.3 Freeze 重写（DatabaseFreezeNotification + stream 即时/pending commit-rollback，db_path 键）；§4.x DataClient 表述全部改 DataClientPool；"33 种消息类型"→52；WorkerAgentContext 改 std::function；消息表 BackupTaskMessage→BackupRequestMessage、DatabaseFreezeMessage→DatabaseFreezeNotification。
- `docs/task/module.md`：TaskScheduler "FIFO" → 优先级调度 + locality 数据亲和。
- `docs/agent/module.md`：核心数据结构 db_id 键 → db_path（对齐 ADR 0002，删 db_registry_）。
- `docs/solver/module.md`：组件表补 ras_graph_daemon.py / project.py / dbs.py / flows.py。

### 状态标记回填（同日，第二轮）
- `ARCHITECTURE_REVIEW.md`：§2.6 标已解决（6c82ec9 Config 线程安全）；§2.7 标已解决（S7-1 分片 shared_mutex）；§3.6 标已解决（de27cf9 于 2026-06-14 引入 write_buffers + EV_WRITE 异步排空，reactor 发送路径无同步等待；`send_all/sendv` 的 5s poll 仅存在于数据面专用线程）。
- `roadmap.md`：A1（locality 分层）标已完成（1b2ad12）；F5（任务优先级）标已完成（500880c）；F2/F3 标注仍未实现。
- `code-audit-2026-08-08.md`：§2.1 `.pyt` 死代码结论标反转（机制已启用并全量迁移）。

---
---

## 2026-07-31: WorkerInfo 收编 hostname/ip — 消除 worker 数据 4 容器散落 + 并发隐患

### 背景
worker 的网络拓扑属性（hostname/ip）此前散落在 master 的 2 个并行 map（`worker_to_hostname_`/`worker_to_ip_`），与 `WorkerManager::WorkerInfo`（持有 status/capabilities/heartbeat 等调度状态）分离。`add_worker_hostname`/`get_worker_hostnames` 等 API 横跨两套数据源。

### 改动
- `WorkerInfo`（`task/cpp/worker_manager.h`）新增 `hostname_`/`ip_address_` 字段。
- `WorkerManager::register_worker` 增加 hostname/ip 参数（带默认值，兼容旧调用）；新增 `set_hostname`/`get_hostname`/`get_ip_address`，统一受 `mutex_` 保护。
- master 删除 `worker_to_hostname_`/`worker_to_ip_` 两个并行 map，所有读取改经 WorkerManager。
- `select_backup_worker` 从原"hostname map 遍历 + 逐个 get_worker 查 status 的双源 join"简化为一次 `get_all_workers()` 遍历（WorkerInfo 同时含 hostname+status）。
- `add_worker_hostname`/`get_worker_hostnames` API 保留（转发到 WorkerManager），测试无需改动。

### 修复的并发隐患
原 `worker_to_hostname_`/`worker_to_ip_` 的所有访问（注册写、record_worker_info 读、backup/rebuild 遍历）**均无锁**，reactor 线程注册 worker 写 hostname 与其他线程读存在数据竞争。收编进 WorkerInfo 后由 `WorkerManager::mutex_` 统一保护（`get_all_workers`/`get_hostname` 锁内拷贝返回）。

### 文档影响
无活跃文档描述 `fly::WorkerInfo` 字段清单或 `worker_to_hostname_` map（`docs/db-merge-design.md` 引用的 `get_worker_hostnames()` API 签名不变，仍准确）。`docs/adr/0001` 等历史文档提及的 `WorkerInfo` 是 storage 模块的 db_meta 持久化结构（同名不同物），不受影响。

---

## 2026-07-31: WriteRecord 合并 current_writes_/sizes_ + 修复 TaskComplete size 上报死代码

### 背景
worker 侧 task 写入记录此前由两个并行容器持有：`current_writes_`（`CMVector<CMString>` 对象全名）+ `current_write_sizes_`（`CMUnorderedMap<CMString,int64_t>` 全名→压缩字节数）。二者须同键同生命周期，push/clear 成对调用。

### 改动
- 新增 `WriteRecord { full_name_; size_bytes_ }`（`worker_agent.h`），`current_writes_` 改为 `CMVector<WriteRecord>`，删除 `current_write_sizes_` 并行 map。
- `record_write` push 一条 WriteRecord；`end_task` 返回 `CMVector<WriteRecord>`，不再分别 clear。
- 消费方（commit_task_segments / cleanup_failed_task_writes / TaskFailedMessage.dirty_objects_）改为取 `.full_name_`；TaskComplete 的 `written_objects_` 直接用 WriteRecord 的 size。

### 修复的潜在死代码（size 上报恒为 0）
原 `end_task` 先 `current_write_sizes_.clear()`，随后 `TaskComplete` 构造时从该 map 查 size —— map 已空，`written_objects_[i].size_bytes_` 恒为 0。该值用于 data locality 调度亲和度打分（`RemoteObjectInfo.size_bytes_`）。
**实际影响评估**：master 的 `WriteRegister` 路径（`do_write_register`）在写入时已带正确 size 调 `update_remote_idx`，且契约"size==0 时保持已有值不变"使 TaskComplete 的 0 不会覆盖正确值 —— 故此为永远走不到预期效果的死代码，无活跃故障。本次随容器合并让其如其注释所述工作（"实际写出对象（含 size）"），运行时行为不变（QA 全绿佐证）。这也补完了 `docs/locality-scheduling-plan.md` 原设计要求的"current_writes_ 携带 size"（实现时退化成了双容器）。

### 新增/更新文档
- 更新 [`docs/agent/module.md`](agent/module.md) — `current_writes_` 类型说明改为 `CMVector<WriteRecord>`

---

## 2026-07-31: TaskSubmissionSpec — task 数据结构统一（消除字段复制漏改）

### 背景
task 数据此前散落在 7 个结构（TaskMetadata / FailedTaskRecord / TaskRequirements / 2 个网络 message / PendingTask / Python 提交参数）+ 3 个并行 map（task_modules_ / task_args_ / task_vars_），共 13 个手动逐字段复制点。新增 priority 字段时因此漏改（`FailedTaskRecord` 漏存、`restart_failed_tasks` 漏传），暴露结构性缺陷：加一个字段需手动同步 5+ 处，漏改几乎不可避免。

### 改动（组合模式收敛）
- **新增 `TaskSubmissionSpec`**（`src/task/cpp/task_manager.h`）：提取 task 提交时全生命周期不变的字段集（name/module/args/inputs/outputs/caps/attribute_timeout/priority/write_context_hash/vars）为单一 struct，含 `FLY_SERIALIZE`。
- **`TaskMetadata` / `FailedTaskRecord` 组合复用** `submission_`，而非各自重复声明同名字段。`FailedTaskRecord` 的 `FLY_SERIALIZE` 从 10 字段塌缩为 `FLY_SERIALIZE(task_id_, submission_, error_message_)` —— 新增字段自动随 spec 序列化，从根上消除"漏加序列化 → 静默丢字段"。
- **删除 3 个并行 map + `task_args_mutex_`**：module/args/vars 统一从 `metadata_->get_task(id)->submission_`（shared_ptr 快照，线程安全）读取，消除"两段式上锁拷贝"。
- **`MasterAgent::submit_task`** 主签名改为 `(task_id, const TaskSubmissionSpec&)`（2 参数），消除 11 个位置参数的错位风险；保留位置参数便利重载服务测试。
- **4 处 FailedTaskRecord 构造收敛为 1 个 `make_failed_record(task_id, error_msg)` helper**（`submission_` 整体拷贝）。
- **`restart_failed_tasks`** 改为 `submit_task(id, record.submission_)` 一行，彻底消除位置参数漏传。
- **顺带修正**：`task_export.cpp` 的 `create_task` export 此前漏 priority 参数（"加字段漏一处"的活证据），现随 spec 统一修正。

### 不改动（边界）
- `TaskRequirements` 保持独立（DependencyGraph 的调度视图，含 locality_hint_，职责不同）。
- 网络消息字段集 / wire format 不变（TaskSubmitMessage / TaskAssignMessage 构造时从 spec 取所需字段）。
- Python 签名不变（`as_task` / `submit` 关键字参数保持，组合改造在 C++ 层）。

### 新增/更新文档
- 更新 [`docs/agent/module.md`](agent/module.md) — 移除已删除的 task_modules_/task_args_，补 submission_ 单一来源说明
- 更新 [`docs/redundancy-audit-report.md`](redundancy-audit-report.md) §3.5 — 标注"三 map 有意分离"决策已推翻
- 更新 [`docs/ARCHITECTURE_REVIEW.md`](ARCHITECTURE_REVIEW.md) §2.8 — task_args_mutex_ 嵌套锁风险已随 map 删除而解决

### 关键结论
用组合模式收敛复制点，新增 task 提交字段时只需改 `TaskSubmissionSpec` 定义 + 其 `FLY_SERIALIZE` 一处，所有持有方（TaskMetadata / FailedTaskRecord / 经 spec 构造的 message）自动获得该字段。priority 式漏改从结构上不再可能。

---

## 2026-07-31: Task Priority（任务优先级）实现

### 背景
`docs/roadmap.md` §五 [F5] 标注 P1。scheduler 此前纯 FIFO（task_id 升序），无法表达"多流程并行时某条流程更重要""后台清理让路"等优先级需求。

### 改动
- `TaskRequirements` 加 `int priority_ = 10`（默认中点值，双向可调：<10 让路，>10 抢先）。
- `get_ready_tasks()` 按 `(priority desc, task_id asc)` 排序；head-of-line skip（高优先级缺 worker 不阻塞低优先级）。
- 全链路透传：TaskMetadata（崩溃恢复）+ TaskSubmitMessage（worker→master 递归提交）+ Python `@as_task(priority=N)` 独立关键字。
- 完全向后兼容：所有现有 task 默认 10（同值），排序退化为 task_id 升序 = 现状 FIFO。

### 新增/更新文档
- 新增 [`docs/priority-scheduling-design.md`](priority-scheduling-design.md) — 设计方案 + 新旧示例
- 更新 `docs/roadmap.md` — [F5] 标记 ✅ 已完成
- 更新 `docs/architecture.md` §3.2 — 任务调度策略补 priority 说明

### 关键结论
默认值取中点 10（非 0），让优先级双向可调。决定向后兼容性的是"默认值是否全部一致"，而非值为多少。

---

## 2026-07-22: DB Merge 设计方案（v3 — 对齐设计契约 + 主动 API）

### 背景

`docs/architecture.md` §5.3 设想的 Database Freeze 后处理长期未实现。经多轮源码核实 + 设计文档核对，
方案收敛为：提供 **`fly.merge_db(path)` 主动 API**（用户显式调用，不绑 freeze），把分散在各 worker
本地 `data_path` 的 `.dat` 数据通过网络聚合到 master 可达的共享 `base_path`，产出自包含数据库目录。

### v2 → v3 关键修正（对齐 `architecture.md §3.3` 双路径设计契约）

| 项 | v2 | v3 |
|----|----|----|
| base_path 共享性 | "场景相关"（含糊） | **设计契约：共享**（§3.3 明确"所有 Master/Worker 可访问"） |
| 本地化对象 | idx + data 都可能本地 | **仅 `.dat`（data_path）本地**；idx/meta 在共享 base_path |
| idx 传输 | 判必要（需 IdxRequest/Response） | **冗余**——索引已在共享盘，master 直读 `<writer_id>.idx` |
| `.dat` 传输 | 提及 | **核心**，复用 backup 已验证的 `read_raw_compressed` + `do_backup_write`（零解压落盘） |
| 触发时机 | freeze 自动/离线 | **用户主动 API**（`fly.merge_db`） |
| 新消息类型 | 新增 IdxRequest/Response | **不新增**（复用 DATA_REQUEST/RESPONSE msg=11/12） |

### 新增/更新文档

| 文件 | 内容 |
|------|------|
| `docs/db-merge-design.md` | v3 完整方案：双路径契约（§1）、复用 backup 范式（§2，含时序图）、`merge_db` API 4 阶段流程（§3）、实现触点（§4，不新增消息/模块）、7 个开放问题、v1→v2→v3 变更说明 |

### 关键结论（v3）

- **索引/元数据天然全局可见**（在共享 base_path），master 可直接 `LocalIndex::load` 读所有 `<writer_id>.idx`。
- **真正本地化的是 `.dat` 数据本体**（`data_path`），这才是 merge 要搬运的对象，必须走网络。
- **backup 机制已实现完整的跨机 `.dat` 搬运范式**（`read_raw_compressed` → `DATA_REQUEST/RESPONSE` → `do_backup_write`），merge 直接复用，不造新轮子。
- **不新增消息类型、不新增模块、不占 task 槽**（走数据面直连，仿 `db.backup_object` 手动路径）。

## 2026-07-22: DB Merge（Freeze 后处理）设计文档（v2，修正数据本地性前提）

### 背景

`docs/architecture.md` §5.3 设想的 Database Freeze 后处理（idx 合并 → `merged.idx` + `_META` 聚合）长期未实现（`docs/roadmap.md` §4 决策②已将 F2 降级）。经 2026-07-22 源码核实，缺口分三层：idx 未 compact、`removed_objects_` 的 `.dat` 物理数据未回收、跨 worker idx 聚合完全空白。

### v1 → v2 关键修正

v1 基于错误前提"master/worker 共享 FS，可直读 idx 文件"，故判定 `IdxRequest/IdxResponse` 冗余、`merged.idx` 价值有限。用户纠正：**fly 多机运行时每个 worker 在本地磁盘写 db 数据，`.dat` 不共享，读走 DataServer TCP**（`data_writer.cpp:23` data_path 可独立于 base_path；`data_server.cpp` 网络服务数据）。据此重写：

| 项 | v1（错误） | v2（修正） |
|----|-----------|-----------|
| 数据本地性 | 假设共享 FS 直读 idx | 多机本地磁盘：`.dat` 各 worker 本地，读走 TCP；idx base_path 共享性场景相关 |
| IdxRequest/Response | 判定冗余不实现 | **多机本地磁盘下必要**，纳入方案 C（枚举槽位 15/16 已预留） |
| merged.idx 价值 | "仅 worker 不可达的 fallback" | "让 master 不依赖各原 worker 本地 idx 可达即持有全局调度视图"——核心价值 |

### 新增文档

| 文件 | 内容 |
|------|------|
| `docs/db-merge-design.md` | DB Merge v2 方案：数据本地性模型（§1）、现状缺口三层（§2）、load_db 局限（§3）、分阶段方案（A: idx compact 纯本地 / B: `.dat` compaction 纯本地 / C: 网络聚合 merged.idx 跨机）、7 个待确认开放问题（含 Q7 多机 base_path 一致性）、v1→v2 变更说明 |

### 关键结论（v2）

- **缺口 A/B（idx compact、`.dat` compaction）与本地性无关**，纯本进程文件操作，任何部署形态都该做，是纯收益。
- **缺口 C（跨 worker 聚合）的价值恰在多机本地磁盘场景**：让 master 在不依赖各原 worker 机器本地 idx 可达的前提下，持有一份全局索引视图，用于依赖图可见性/调度/对象存在性。freeze 是天然聚合时机（全员已 flush，idx 稳定）。
- **merged.idx 只聚合索引，不聚合数据本体**——读取仍需网络回源到原 worker DataServer。数据冗余是 backup 机制（§5.4）职责，与本方案正交。

## 2026-06-30: 网络感知远程读优先级（NetQualityMonitor + 带宽探测）

### 背景

集群中不同机器间的时延、带宽不对称。`read_object` 远程读（TIER2 多副本轮询）此前按副本**注册顺序**遍历，与连接质量无关——慢链路的副本会被优先尝试，拖累整体读路径，无法利用集群网络带宽加速。

### 改造

新增**网络感知读优先级**：远程读优先向连接性最好的副本请求。两个子功能：

- **后台带宽/连接性测试服务**：每个 worker 的 `bandwidth_probe_thread_`（仿 heartbeat 四件套，`net_probe_enabled` 控制）周期性探测 `DataService::get_all_workers()` 返回的 peer，发 `NET_PROBE_REQUEST`，peer 的 DataServer 回 `NET_PROBE_RESPONSE`，测 RTT + 带宽。同时被动 RTT 在真实远程读（`DataClientPool::request`）时零成本采集。
- **read_object 触发远程读时按连接性排序**：TIER2 取出副本后用 `std::stable_sort` + `NetQualityMonitor::score(host)` 降序排序，等分（含冷启动无数据）保持注册顺序兜底。`net_probe_enabled=0` 时排序降级为 no-op，零回归。

### 文档更新

| 文件 | 更新 |
|------|------|
| `docs/core/module.md` | 配置项表新增 `net_probe_enabled`/`net_probe_interval_ms`/`net_probe_payload_kb`/`net_probe_timeout_ms` |
| `docs/network/module.md` | 组件表 + 新增 `## NetQualityMonitor` 章节（数据来源、评分排序、分层） |
| `docs/storage/module.md` | TIER2 描述加入网络质量排序；DataServer 加入消息 dispatch（DATA_REQUEST + NET_PROBE_REQUEST） |
| `CLAUDE.md` | 网络层文件表加 `net_quality_monitor`；消息枚举数更新为 40 |

## 2026-06-29: 读路径多副本容错 + TIER2 指数退避重构

### 背景

此前 `read_object` 的远程读路径是「单副本单次直连 + pool 内部 DATA_NOT_READY 轮询」。
存在三个问题：(1) `lookup_remote_idx` 只取 `workers_.front()`，多副本存储从未被利用，
首副本失败即整体失败；(2) `DataClientPool` 内部无限轮询 DATA_NOT_READY 且无总超时，
与上层退避叠加导致耗时失控；(3) master TIER3 自读 handler 网络失败时 `can_still_produce`
硬编码 false，语义不一致。

### 改造

读路径重构为「TIER2 多副本轮询 + 分类重试 + TIER3 纯位置查询」架构：

- **ReadError 枚举**（`common/cpp/error_types.h`）：NONE/DATA_NOT_READY/OBJECT_NOT_FOUND/
  NETWORK/SHUTDOWN，驱动 TIER2 重试策略。
- **DataClientPool 改单次请求语义**：删除 DATA_NOT_READY 内部轮询，透传 ReadError。
  重试职责上移到 TIER2，避免双重退避叠加。
- **TIER2 多副本轮询**（`DataService::read_raw_compressed`）：
  - `lookup_all_remote_idx` 遍历全部副本，每个试一次
  - OBJECT_NOT_FOUND 删副本；DATA_NOT_READY 无限重试；NETWORK 30s 限；SHUTDOWN 立即终止
  - 指数退避 10ms→500ms ×2 ±10% 抖动（`thread_local std::mt19937`）
- **TIER3 改纯位置查询**：查 master 全部副本 → 回填 remote_idx → 重入 TIER2
  （`tier3_queried` 标志防 TIER2↔TIER3 弹跳）。DataLocationMessage 协议改为
  `CMVector<DataLocation> locations_`（多副本，不向后兼容）。
- **master 读路径**：TIER1 → TIER2（注册 direct handler，用 DataClientPool，与 worker
  对称）+ 本地非网络 TIER3 handler（返回 `can_still_produce`，供 wait_obj 判断对象是否
  可能仍被产出）。
- **预取回填 remote_idx**：删 `prefetched_locations_` 临时缓存，`on_task_assign` 收到
  `dependency_locations_`（多副本）时直接全部回填 remote_idx，使首轮读命中 TIER2。
  统一为单一数据源。

### 调试中修复的两个真实回归

1. master 删 TIER3 后丢失 `can_still_produce` 信号 → wait_obj 误判对象无法产出而 raise。
   修复：恢复 master 的 TIER3 handler 为纯本地查询（不走网络，不违反「master 不 self-
   DataQuery」本意）。
2. master `data_client_pool_` 在 `do_drain_and_stop` 中被 stop，与 master 对象的
   start/stop/start 复用模式冲突（DataClientPool.stop 单向不可逆）。修复：移除该
   stop，pool 随 master 析构（=进程退出）清理。

### 验证

- 单元测试：49/49（新增 `data_client_pool_test` 验证 DATA_NOT_READY 透传/error 分类；
  `data_service_test` 新增 `lookup_all_remote_idx`、TIER2 failover、副本删除用例）
- 全量 QA：110/110
- 高并发回归：5 轮 × 110/110 = 零失败

### 文档更新

- `docs/storage/module.md`：重写「读取流程」三级 fallback 流程图、新增「ReadError
  分类与重试策略」表、更新 remote_idx 多副本描述、远程索引更新时机、设计决策表。

---

## 2026-06-28: solver QA flaky 修复（wait_obj 等待序列最后一个对象）

### 现象

全量 QA 高并发跑 solver 时偶发 flaky，失败 case 为 `test_solver_ras_n6_sd2_ov2.py`
（及同族 `test_solver_ras_*`）。复现率：同 case 6 并发跑 120 次失败 3 次；全量
并发第 1 轮即可复现。报错固定为：

```
EOFError: Ran out of input
  File "solver/ras.py", line 134, in get_ras_solution
    "residual": db.read_object("__ras__final_res"),
```

### 根因

`ras_check` 收尾时在**同一个 worker** 内串行写出 4 个对象，顺序固定：

```python
db.write_object("__ras__sol", x)          # ①
db.write_object("__ras__final_res", res)  # ②
db.write_object("__ras__iters", nxt)      # ③
db.write_object("__ras__ok", ...)         # ④ 最后
```

而 master 侧 `get_ras_solution` 的 `@wait_obj` **只等 `__ras__sol`（①）**，解除阻塞
后在函数体内读 ②③④。`write_object` 的 WriteRegister 是同步往返 ack，但 master 收到
① 的 WriteRegister 并 `mark_data_ready` 后即解除 `@wait_obj`，此刻 ②③④ 的
WriteRegister 尚未到达 master（高并发下 CPU 抢占放大了这个窗口）→ `read_object`
对空数据解 pickle 抛 `EOFError`。典型的 read-after-ready 竞态。

### 修复

`@wait_obj` 的等待目标从 `__ras__sol` 改为 `__ras__ok`（序列中最后一个）。由于
同一 worker 内 WriteRegister 串行同步往返，等到 `__ras__ok` 就绪即蕴含 ①②③ 早已
注册可读（详见 `docs/storage/module.md`「同一 worker 内连续写入的顺序保证」）。

- `src/solver/py/ras.py::get_ras_solution`：`@wait_obj` inputs 从
  `[__ras__sol]` → `[__ras__ok]`
- `docs/storage/module.md`：新增「同一 worker 内连续写入的顺序保证」节，明确
  stream 模式（默认）下「等待序列最后一个对象即可安全读取前置伴随对象」的语义，
  并记录反模式
- `ras_graph.py` 的 `get_ras_graph_solution` 经核查**无需改**：其 `__rasg__sol`
  是收尾序列（converged → iters → sol）的最后一个、且是依赖图锚点，等 sol 已正确

### 验证

- 同 case 6 并发压测 180 次：0 失败（修复前 120 次失败 3 次）
- 全量 solver QA 高并发 10 轮：每轮 25/25 全过（修复前第 1 轮即失败）

---

## 2026-06-28: write register 可见性延迟（非 stream 模式 task 级原子性 WP2）

### 非 stream 模式下 mark_data_ready 延迟到 task 完成

`do_write_register` 拆分为校验段（provenance + frozen 检查，两种模式都即时）和可见性登记段
（mark_data_ready + update_remote_idx + schedule_tasks）。非 stream 模式下可见性登记
延迟到 `on_task_complete` 的 written_objects_ 统一处理，保证 task 失败回滚后下游 task
不会被错误调度。`on_task_complete` 非 stream 分支补齐 `update_dependency_location_cache`。

- `master_agent.cpp::do_write_register`：可见性登记段按 `streaming_mode` 分流（master 自写 worker_id_==0 强制即时，无 TaskCompleteMessage 触发时机）
- `master_agent.cpp::on_task_complete`：非 stream 分支补 `update_dependency_location_cache` + `record_worker_info`（db_id 由 master 用 `split_full_name` 从 object_name_ 反解，不冗余传输）
- `master_agent.h`：`on_task_complete`/`on_task_failed` 移至 public（供测试直接调用）
- `database.h/cpp`：新增 `worker_info_count()`（读 _DB_META 统计 worker 记录数，供测试验证）

---

## 2026-06-28: Freeze 延迟可见 + ack 通道 + 崩溃恢复（WP1）

### freeze 通知双路径冗余消除 + 非 stream 模式 task 级原子性

freeze 从"差集推断 + 延迟补发"重构为"task 内主动即时通知 + 按 task_id 提交/回滚"。
非 stream 模式（`dependency_update_mode != 0`）下，freeze 在 task 内声明为 pending，
task 成功才迁移到 confirmed + 广播；task 失败/崩溃按 task_id 回滚（防永久死锁）。

- `message_types.h`：`DatabaseFreezeNotification` 新增 `task_id_`；新增 `DatabaseFreezeAckMessage`（success + error_type）；`DATABASE_FREEZE_ACK=39`
- `error_types.h`：新增 `TaskErrorType::DB_ALREADY_FROZEN=7`（冲突 fail-fast）
- `master_agent.h/cpp`：新增 `pending_frozen_dbs_`（map<db_id,task_id>）；`is_db_frozen` 改查 confirmed ∪ pending；新增 `is_db_pending_frozen` / `commit_pending_frozen` / `rollback_pending_frozen`；`on_database_freeze_request` 分流（stream 即时 / 非 stream pending）+ 冲突检测回 ack；`on_task_complete` 调 commit；`on_task_failed`/`on_disconnect` 调 rollback
- `worker_agent.h/cpp`：`request_database_freeze` 从 fire-and-forget 改同步等 ack（pending+cv，5s 超时）；`DatabaseFreezeNotification` 带当前 task_id；新增 `on_database_freeze_ack` handler + reactor 注册
- `executor.py`：删除 frozen 差集计算（遍历 `_db_cache` 两次 + 前后快照）；freeze 由即时通知 + task_id 提交负责

---

## 2026-06-28: 数据 Locality 调度 + 写入注册统一 + size 链路

### 数据 Locality 调度（Config `locality_scheduling_enabled`，默认 1 开启）

scheduler 按数据亲和度选 worker：对每个 ready task，计算各 worker 持有其输入数据的总量（score），
选 score 最大且不降低 capability 匹配质量的 idle worker。三阶段算法：capability 完整匹配优先 →
locality 偏好 → 兜底。scheduler 直接查 DataService placement 算分，持久 score 缓冲区复用。

- `task_scheduler.h/cpp`：`locality_enabled_` 开关、`compute_scores`（依赖驱动，score_buf_ 按 worker_id 索引）、`select_best_worker` 三阶段算法
- `dependency_graph.h`：`get_task_requirements` 改返回 `const TaskRequirements&`（无值拷贝）
- `core/config.cpp`：新增 `locality_scheduling_enabled`（默认 1）

### 写入注册统一到 WriteRegisterMessage（删除 DataReadyMessage）

所有写入注册（worker 写 / master 自写 / backup）统一走 `WriteRegisterMessage` → `do_write_register`。
删除冗余的 `DataReadyMessage`（其核心动作 mark_data_ready/update_remote_idx/schedule 已由 WriteRegister 覆盖）。
master 自写改为同步调 `do_write_register`（丢弃 ack，零网络开销）。

- `message_types.h`：删除 `DataReadyMessage` + `MessageType::DATA_READY`；`WriteRegisterMessage` 加 `writer_id_` + `size_bytes_`；新增 `WrittenObject` 结构体
- `master_agent.cpp`：抽 `do_write_register` 纯逻辑函数；`on_data_ready`/`on_master_record_write` 删除；`record_worker_info`/`evaluate_and_trigger_backup` 从原 on_data_ready 抽出迁入 do_write_register
- `worker_agent.cpp`：`record_write` 删除 streaming 分支（保留 `current_writes_` 收集）

### size 链路（RemoteObjectMeta.size_bytes_）

数据对象的压缩后字节数随写入注册传递到 master placement table，供 locality 调度亲和度打分。

- `data_service.h/cpp`：`RemoteObjectMeta` 加 `size_bytes_`；`update_remote_idx` 加 size 参数（size>0 才更新，size==0 保持原值，防御 rebuild 路径）；新增 `get_remote_size`
- `worker_context.h`：`register_write`/`record_write`/`set_register_func`/`set_record_write_func` 签名加 `int64_t compressed_size`
- `database.cpp`：`commit_write`/`do_backup_write`/`put_temp_data` 三处 register 调用带 size
- `TaskCompleteMessage.written_objects_` 改为 `CMVector<WrittenObject>`（含 size）

### 模块依赖

task 模块（本质是调度模块）新增对 storage 的依赖（scheduler 查 DataService placement 算分）。
`task/cpp/BUILD` 加 `fly_storage` 依赖，`fly_task_so` 用 `dynamic_deps` 引用 `fly_storage_so`。

> ⚠️ **2026-06-30 已撤销**：见下方 2026-06-30 条目。scheduler 改为消费 master 预计算的
> `locality_hint_` POD，task→storage 依赖重新解除，恢复六层架构 BUILD 级无环。

---

## 2026-06-30: Locality 分层解耦 + 长时运行内存分析

### 架构修复：task→storage 分层依赖解除

`scheduler` 不再直接查 `DataService`，改为消费 master 预计算的 locality hint：

- `TaskRequirements` 新增 `locality_hint_` 字段（POD，`CMVector<std::pair<uint64_t,int64_t>>`，worker_id→持有输入字节数）。不参与序列化（`TaskRequirements` 不跨进程）。
- `DependencyGraph::set_task_locality_hint()` 新增 setter。
- `TaskScheduler::compute_scores()` 改读 `locality_hint_`，删除 `DataService::instance()` 调用。
- `src/task/cpp/BUILD` 删除 `fly_storage` / `fly_storage_so` 依赖；`task_scheduler.h` 删除 `<storage/cpp/data_service.h>` include。
- `MasterAgent::schedule_tasks()` 入口预计算 hint 注入 graph（master 合法持有 DataService）。
- 测试 T1–T7 改用 `inject_hint` 注入，移除 DataService singleton 依赖。
- 验证：`bazel query deps(//src/task/cpp:fly_task)` 依赖闭包零 storage；单测 50/50 + QA 111/111。

### 新增文档

- `docs/roadmap.md` — 增强路线图（P0 locality 已完成；P1 含 F5 优先级 / F3 role / M1 内存观察项）
- `docs/locality-decoupling-fix-plan.md` — 解耦方案与验证清单
- `docs/memory-growth-analysis.md` — 长时运行内存增长分析（数据对象元信息无上限累积，十万级可接受）
- `docs/competitor-analysis.md` — 竞品分析（此前已生成，本次随附）

### 受影响文档同步

- `docs/architecture.md` §3.2 locality 描述：从"scheduler 直接查 DataService"改为"消费 master 预计算 hint"。
- `docs/locality-scheduling-review.md` §1/§5 P0：标记 RESOLVED。

---

## 2026-06-25: 失败 Task 脏数据清理（事务化段标记 + 异常清理）

### idx op log 事务化段标记

LocalIndex 新增 BEGIN/END/ABORT 三个段边界标记（不含 task_id）。worker task
写入被 BEGIN/END 包裹，ADD 进 pending 区，END 提交 / ABORT 回滚。崩溃遗留的
未闭合段在 load_db 时自动丢弃（pending 区语义）。

- `local_index.h/cpp`：新增 IdxOpType::BEGIN/END/ABORT、mark_begin/end/abort、
  had_unclosed_segment 诊断、load pending 区状态机
- `data_writer.h/cpp`：mark_begin 记录 data 偏移回滚点；abort_segment 执行
  data 文件 truncate（含跨 rollover 多文件）
- `write_back_queue.h/cpp`：新增 clear_pending 丢弃未落盘脏写（比 drain 高效）

### 异常清理路径

- `database.h/cpp`：abort_task_writes（clear_pending + ABORT + truncate + 清内存）
- `worker_agent.cpp`：BEGIN 在 task 首次写入打（WBQ execute lambda）；成功打 END；
  失败走 cleanup_failed_task_writes
- `worker_context.h`：新增 transaction_mode 区分 worker task 与 master 写入
- `message_types.h`：TaskFailedMessage 新增 dirty_objects_ 字段
- `master_agent.cpp`：on_task_failed 清理 dirty_objects 的 remote_idx/provenance/
  依赖图 + 广播 OBJECT_REMOVED

### 连带修复

- `master_agent.cpp`：on_task_failed 增加持久化 failed task（之前只有调度失败
  才持久化）；schedule_tasks 依赖不可解检测移到 fail_unscheduleable_tasks
  开关之前（修复上游失败后下游 pending task 40s 才判失败的延迟）

### 文档更新

- `docs/issues/001-failed-task-rerun-write-duplication.md`：状态改为 Resolved，
  追加最终解决方案章节
- `docs/storage/module.md`：补充写入事务语义 + WriteBackQueue clear_pending

### 测试补充（大对象 + 多对象跨文件）

- DataWriter 单测 +3：AbortLargeObjectInEmptyFile / AbortLargeObjectTriggersRollover /
  AbortMultipleObjectsAcrossFiles（验证 abort 的 data 文件 truncate 含跨 rollover 多文件）
- QA test_mixed_write_fail：大对象(>1MB)触发 rollover + 多小对象跨文件 →
  task 失败 abort → load_db 脏数据不恢复 → restart 大对象数据正确

---
---

## 2026-06-23: FlyStream C++ 基础设施 + __getstate__/__setstate__ + 宏重命名

### FlyStream — 流式序列化+压缩容器（C++ 基础设施）

新增 `FlyStream` 类（`src/storage/cpp/fly_stream.h`），流式序列化+压缩容器。
已导出到 Python（`_fly_storage.FlyStream`），但 database.py 暂未集成
（worker 模式 DataService remove 死锁问题待排查）。

### FLY_EXPORT_SERIALIZE 宏更新

补回标准 pickle 协议 `__getstate__` / `__setstate__`，支持 C++ 对象放入
Python 容器（list/dict/nested）。与 `__getstate_buffer__` /
`__setstate_from_buffer__`（零拷贝路径）共存。

### 序列化宏重命名

- `FLY_ENCODE_TO_BYTES` → `FLY_ENCODE_TO_BUFFER`（原名误导）
- `FLY_DECODE_FROM_BYTES` → `FLY_DECODE_FROM_BUFFER`

### stress_stability 测试修复

- 根因：`kMaxCompletedTasks=100` 限制 completed bucket，测试 150 task 超限
- 修复：减小测试规模到 90 task（60 writes + 30 sums）


## 2026-06-22: var 小数据存储服务 + get_obj_name→get_full_name 重命名

### var 小数据存储服务（db.set_var / get_var / remove_var）

新增轻量级小对象 KV 服务，绕开 `write_object` 的压缩/缓存/WriteBackQueue/依赖图全套机制：

- **db 由 db 直接管理**：Database 内建 `var_store_`（FlyBufferPtr 载体），master 进程 Database 实例为权威存储，worker 经 WorkerAgentContext 同步到 master。
- **零拷贝**：内存层全程 FlyBufferPtr 共享；消息边界用 `mutable` 字段 + `std::move`；Python 对象用 FlyBuffer 的 file-protocol（`pickle.dump(value, buf)` / `pickle.load(buf)` via readinto）；C++ 对象用 `__getstate_buffer__` / `__setstate_from_buffer__`。
- **全程全名**：var 名用 `db.get_full_name(name)`（`db_id:short_name`），消息无冗余 db_id，master 用 `split_full_name`（基于 db_id_len 固定切分）定位 Database。
- **隐式依赖**：set_var/get_var 同步，依赖 master reactor 单线程 FIFO 保证"set_var 后 write_object 的数据依赖满足时，var 一定可取"。
- **@as_task(vars=...)**：task 声明所需 var，master 调度时 inline 带入 TaskAssignMessage。
- **freeze 持久化**：freeze 时 `_VARS` 文件持久化未删除 var，load_db 恢复。
- **不变性**：var 写入后不可改，重复 set 被拒绝；freeze 后 set_var 被拒绝。

### get_obj_name → get_full_name 全仓重命名

var 与 object 共用 `db_id:short_name` 命名空间，`get_obj_name` 名称对 var 有歧义，统一为中性 `get_full_name`。涉及 solver/mapreduce/e2e_tasks/单测等 30+ 处。

### FlyBuffer file-protocol 接口

FlyBuffer 新增 `read(n)` / `readline()` / `readinto(bytearray)` / `seek(n)` / `pos`，支持作为 `pickle.load` 的 file-like 对象（readinto 零拷贝写入 pickle 工作缓冲），消除 var get_var 的中间 Python bytes 拷贝。

### executor 三阶段执行

重构 worker task 执行为 `preprocess`（db 创建/注册 + var 注入）/ `execute`（调用 task 函数）/ `postprocess`（空函数预留扩展点）。

### FLY_EXPORT_SERIALIZE 序列化接口

移除 `__getstate__`/`__setstate__`（bytes 版，无生产使用），改为 `__setstate_from_buffer__(FlyBufferPtr)`（零拷贝反序列化填充）。

---

## 2026-06-21: attribute_timeout — 属性依赖超时降级

### `@as_task(requires=...)` 支持 tuple/callable 形式

`requires` 参数扩展，新增属性依赖超时语义：

- `list[str]`：死等（旧行为，向后兼容）。
- `tuple(list[str], float)`：`(能力标签, 超时秒数)`，`timeout<0` 死等 / `==0` 立即降级 / `>0` 限时降级。
- `callable(*args, **kwargs)`：提交时动态解析为上述任一形式。

### 调度器限时降级机制

新增 master `attr_timeout_check_thread_`（周期 200ms）周期性触发 `schedule_tasks()`，让限时等待属性的 task 在数据依赖满足后到期被降级调度到匹配属性最多的 idle worker。

### 文档同步

- `docs/python-api/module.md`：更新 `as_task` 签名、requires 形式、attribute_timeout 语义。
- `docs/task/module.md`：调度算法段落补充属性匹配与超时降级表。

---

## 2026-06-20: solver 优化 + stop() 修复 + FLY_RELEASE + 粗网格预构建

### stop() 流程重构

**三阶段流程**：
1. Phase 1: 等待所有 running tasks 完成（workers 仍然活跃）
2. Phase 2: 发送 shutdown 给所有 workers
3. Phase 3: 等待 workers 断开连接（CV 通知机制）

**draining 模式修复**: on_disconnect 在 draining 模式下将 running tasks 标记为 FAILED（而非跳过），并通知 drain_cv_，避免 stop() 等待 10s 超时。

**自动 stop()**: 脚本模式下，用户脚本执行完毕后自动调用 stop()。交互模式下，用户退出时通过 atexit 调用 stop()。

### FLY_RELEASE 编译 flag

新增 `FLY_RELEASE` 编译宏，在 `build:opt` 模式下自动定义。DBG 宏在 FLY_RELEASE 模式下编译为空宏 `((void)0)`，彻底消除热路径日志开销。

配置方式：`./fly.sh build --config=opt`（等效于 `--compilation_mode=opt -DFLY_RELEASE`）

### scipy 模块级 import

将 `numpy`、`scipy.sparse`、`scipy.sparse.linalg.splu` 移到 `ras_graph.py` 顶部，避免热路径懒加载。Worker 进程启动时即完成 import，不阻塞迭代。

### 粗网格预构建

coarse 校正的粗网格构建从迭代循环内移到迭代前。通过 `_prebuild_coarse_grid()` 向所有 worker 分发构建任务，worker 并行构建，不阻塞 check task。

### 热路径日志降级

- `data_server.cpp`: DS-ACCEPT/DS-Q/DS-SEND INFO→DBG
- `master_agent.cpp`: WriteRegister INFO→DBG

### 性能对比 (O2 + FLY_RELEASE, golden_n50_sd9)

| 版本 | Wall Clock | t_total | read_nb | write |
|------|-----------|---------|---------|-------|
| Baseline | 5578ms | 10.7ms | 6.2ms | 2.0ms |
| 优化后 | 3368ms | 4.8ms | 2.4ms | 1.0ms |
| 提升 | -39.6% | -55% | -61% | -50% |

### n=500 coarse 性能

| 阶段 | 优化前 | 优化后 |
|------|--------|--------|
| 粗网格构建 | 迭代内阻塞 | 1.6s (迭代前并行) |
| 迭代时间 | 5.7s | 2.8s (-51%) |
| 总时间 | 7.44s | 6.41s (-14%) |

---

## 2026-06-19: TaskManager/DependencyGraph 性能优化 + 依赖位置预取

### TaskManager 优化

**按状态分桶存储**: 任务元数据按状态分为 5 个桶（PENDING/RUNNING/COMPLETED/FAILED/CANCELLED），按状态查询从 O(n) 降到 O(k)。

**shared_ptr 存储**: 任务元数据使用 shared_ptr 管理，读取时返回 shared_ptr 拷贝（0.2ns），消除数据竞态。

**原子复合操作**: 将常见的多步操作（如更新状态+设置错误）合并为单次锁获取，减少锁竞争。

**O(1) 状态查询**: 新增 has_tasks_with_status、count_tasks_by_status 等 O(1) 查询接口。

**ID-only 查询**: 新增 get_task_ids_by_status、get_task_ids_by_worker，避免拷贝完整元数据。

**自动清理**: 已完成任务超过阈值时自动淘汰最老任务，防止内存无限增长。

### DependencyGraph 反向索引

新增 data → pending_tasks 的反向索引。mark_data_ready 从遍历所有 pending tasks（O(P×D)）改为只检查依赖该数据的任务（O(T×D)）。

### 依赖位置预取

**机制**: Master 在任务提交时缓存依赖数据位置，在任务分配时通过 TaskAssignMessage 下发给 Worker。Worker 读取依赖数据时优先使用预取位置，避免查询 Master。

**效果**: 远程读路径从 2 次网络往返（Master 查询 + 数据读取）减少为 1 次（仅数据读取）。

### sendv 合并发送

DataServer 使用 writev 系统调用将 header 和 payload 合并为一次发送，减少系统调用次数。

### 日志级别修复

热路径中的 INFO 日志改为 DBG，消除每 worker ~3400 条日志的 I/O 开销。

### 性能对比 (O2, golden_n50_sd9)

| 指标 | Before | After | 提升 |
|------|--------|-------|------|
| Wall Clock | 3502ms | 3105ms | -11.3% |
| t_total | 5.6ms | 4.3ms | -23.2% |
| read_nb | 3.4ms | 2.2ms | -35.3% |
| write | 1.0ms | 0.92ms | -8% |

---

## 2026-06-19: temp cache 重构 + 读重试策略 + wait_obj timeout + QA 拆分

**temp cache 重构**: `LocalObjectInfo::temp_compressed_data_` 从 `CMString` 改为 `FlyBufferPtr`。`write_temp_pickle` 直接压缩到 `FlyBufferPtr`，`put_temp_data` → `on_temp_write` 全链路 shared_ptr 透传，写入零拷贝。读取时直接返回 shared_ptr，读取零拷贝。`try_read_local_raw` 统一返回 `FlyBufferPtr`，temp 和非 temp 路径一致。仅淘汰到 `temp_eviction_store_` 时拷贝一次（低频路径）。

**put_temp_data 时序修复**: `on_temp_write`（存储数据到 `temp_compressed_data_`）移到 `register_write` 之前。`register_write` 是同步阻塞的，Master 收到 ACK 后立刻调度依赖任务，如果数据还未存储，其他 worker 读取会失败。

**读重试策略**: `read_raw_compressed` 远程回调改为单次尝试，移除 50ms×30s 重试循环。retry loop 是 workaround 而非正确修复——真正的问题是 Master Tier 3 回调未正确返回 `can_still_produce` 状态。

**can_still_produce 修复**: Master 的 `remote_compressed_read_handler` 回调在数据未找到时检查是否有 pending/running task，返回正确的 `can_still_produce` 状态。旧代码直接返回 `false`，导致 `wait_obj` 误判数据无法产出而报错。

**solver 竞态条件修复**: `ras_graph_check` 中 cleanup 从 `step-1` 改为 `step-2`。原代码中 step N 的 check 删除 `conv_{i}_{N-1}`，但 step N-1 的 check 可能还在读取这些数据（两者依赖不同，可并行执行）。

**依赖图日志**: `dependency_graph.cpp` 添加 INFO 级别日志，追踪 task 依赖注册和 ready 状态变化。task 模块新增 log 依赖（`dynamic_deps`）。

**Master 自读 race 修复**: `remote_compressed_read_handler` 改为直接查 `remote_idx` + `DataClient::request_compressed_data`，不再走 reactor 自查询。原路径经过 epoll，与 worker WriteRegister 在不同 fd 上的处理顺序不保证。

**wait_obj timeout**: `@wait_obj(timeout=30)` 新增可选超时参数。默认 `None` = 永远等待，直到数据可读或确认无法产出（`can_still_produce=false` 确认 3 次）。

**QA 测试拆分**: `test_read_cache.py` 拆为三个独立文件（`test_read_cache_basic.py`、`test_read_cache_cross_db.py`、`test_read_cache_large_objects.py`）。原文件中三个测试函数共享同一 fly 进程，`completed_tasks` 累积导致后续测试的 `wait_for` 被前面的残留数据欺骗。拆分后每个文件由 `runqa` 独立调度，`completed_tasks` 从零开始，保持直接 `read_object` 测试边界条件。

**fly wrapper 修复**: `build/bin/fly` wrapper 脚本设置 `FLY_BUILD` 环境变量，使 `fly.bin` 能自动定位 `build/` 布局，无需用户手动设置。big_qa 测试脚本同步修复环境变量设置。

---

## 2026-06-19: temp 写入路径优化 — 消除 C++→Python→C++ 往返

**问题**: Python `_write_temp` 先调 `_compress_pickle_bytes`（C++ 压缩 → 返回 Python bytes），再调 `_put_temp_data`（Python bytes → C++ CMString）。压缩结果经历 C++→Python→C++ 两次无意义拷贝。

**修复**: 新增 `Database::write_temp_pickle`（C++ 侧一步完成压缩+注册+存储），nanobind 绑定 `_write_temp_pickle`，Python `_write_temp` 直接调用。`storage_export.cpp` 新增绑定。

| 文档 | 变更 |
|------|------|
| docs/storage/module.md | 补充 temp 写入流程说明 |

---

## 2026-06-19: 写入时序重构 + 读写公共路径统一

**写入时序**：`write_object` 统一为 序列化+压缩 → put_low（cache）→ 注册（通知 master）→ 落盘。原时序中注册在序列化之前，master 标记数据就绪时 cache 未填充，其他 worker 读返回 `DATA_NOT_READY` 需重试。

**commit_write 提取**：`write_pickle_bytes`（Python pickle）和 `write_object<T>`（C++ 流式序列化）共享相同的 cache→register→enqueue 逻辑，提取为 `commit_write` 私有方法，净减 56 行。

**读侧公共路径**：两条读路径（Python `_read_decompressed` 和 C++ `read_object<T>`）都经过 `read_object_compressed` 作为公共 IO+缓存+backup 逻辑，无需额外提取。

**读重试参数**：`read_raw_compressed` 远程回调重试从 3 次×1s 改为 50ms×30s。

| 文档 | 变更 |
|------|------|
| docs/storage/module.md | 读路径说明补充 read_object_compressed 公共路径；写入流程更新时序 |
| docs/architecture.md | 写入流程 6 步更新；读重试参数更新 |

---

## 2026-06-19: internal task 判定 + worker config 共享 + DB 路径统一

- `TaskCompleteMessage` 新增 `is_internal_` 字段，替代 `task_id >= 100000` 脆弱判定
- worker 启动共享一份 config 文件（`.fly_config`），不再为每个 worker 创建独立文件
- QA 测试 DB 路径统一到 `log_dir/db`，coordinator 通过 `FLY_DB_PATH`/`FLY_DB_DIR` 环境变量传递共享路径给 helper

---

## 2026-06-19: QA 路径清理 + 分类整理 + 源码 bug 修复

- 删除 98 个 case/helper/script 的冗余 `sys.path.insert`（fly 已自动配好路径）
- 10 个多阶段测试改用 `get_fly_binary()`，消除 `bazel-bin` 硬编码
- `qa/internal/` 7 个 case 按内容拆分到 backup/storage/dependency/fault/unit/
- 源码修复：`read_object_compressed` cache 命中补 backup 检查；`request_db_path` 传 `existing_db_id`；`_update_latest_symlink` 用 `remove_all`

---

## 2026-06-18: 读写路径零拷贝优化 review 修复 — cache 语义 + xsputn 边界

**原因**: review `117c725`（读写路径零拷贝优化）发现三个问题：
1. `cache="none"` 未实现 — `read_object_compressed` 无条件查 low 层，`cache="none"` 仍命中缓存
2. `xsputn` 边界条件 — `buffer_.size() >= chunk_size_` 时 `space<=0`，`to_write<=0`，逻辑不够健壮
3. high-tier 语义回归 — `117c725` 把 `read_object<T>` 改成仅 `cache="high"` 时查/填 high 层，与 `_read_from_db` 设计初衷（C++ class 总是省反序列化）冲突，导致 `test_cpp_object_cache.py` 失败

**修复**:
- `read_object_compressed` 新增 `bypass_cache` 参数，`cache="none"` 时传 `true` 跳过 low 层查询
- `read_object<T>` 恢复重构前语义：`cache="low"`/`"high"` 都查+填 high 层，仅 `cache="none"` 完全 bypass
- `xsputn` flush 检查移到 insert 前，保证 `space` 恒正、`written` 恒前进
- 修正 `117c725` 新增的两个矛盾单测（`ReadObjectLowCacheDoesNotPopulateHighTier` → `ReadObjectLowCachePopulatesHighTier`）

| 文档 | 变更 |
|------|------|
| docs/storage/module.md | `read_object_compressed` 签名加 `bypass_cache` 参数；`read_object` cache 语义说明 |
| CLAUDE.md | object_cache.h 条目补 cache="none" bypass 说明 |

---

## 2026-06-17 (2): 零拷贝验证 — valgrind massif profiling 固化结论

**方法**: 10MB 对象远程传输（master + 2 worker），valgrind massif 追踪各进程堆分配树。

**结论**: wire 路径零用户态 copy。massif 分配树中无 DataResponseProtocol/MessageProtocol/FlyBuffer→CMString/substr/take 相关 heap 分配。worker 峰值分配全在 compress_pickle_bytes（写入压缩）+ decompress_raw_data（解压），均为序列化/压缩/解压的必然成本。

仅剩不可消除 copy: 内核 send/recv（syscall 固有）+ pickle 序列化/反序列化 + lz4 压缩/解压。

---

## 2026-06-17: Wire 协议优化 — DataResponse 分段传输 + string_view 零拷贝 header

**原因**: DataResponseMessage 的 compressed_data_（大对象压缩字节）原经 bitsery 序列化，5 次用户态 copy（500MB/100MB 对象）。改为两段传输（小字段 bitsery + raw payload 独立），消除全部用户态 copy。ObjectHeader::deserialize 改 string_view，消除 header 解析的全量 copy。

| 文档 | 变更 |
|------|------|
| docs/network/module.md | MessageProtocol 帧格式段新增 DataResponseProtocol 两段帧描述 + 方法表 |

改动:
- message_types.h: DataResponseMessage 移除 compressed_data_ 字段（改为传输层 raw payload）
- message_protocol.h: 新增 DataResponseProtocol（两段编解码 + parse_sub_header + raw_len_from_total）
- data_server: serve 用 DataResponseProtocol::encode + SendTask 携 raw_data；do_send 两段发送
- data_client/data_client_pool: 分步 recv（header + sub-header + small_fields + raw 直接进 FlyBuffer）
- object_header.h/cpp: deserialize 改 string_view（CMString/FlyBuffer/raw ptr 零拷贝传参）

消除 copy: wire 用户态 5 次 → 0 次（仅剩内核 send/recv）；header 解析全量 copy → 零拷贝。

---

## 2026-06-16 (4): FlyBufferPtr 全链路零拷贝重构 + write-through

**原因**: ObjectCache low 层原存 CMString，write/read 链路多次 copy。改为存 FlyBufferPtr（shared_ptr 共享所有权），缓存与读取链路全程零拷贝。write_object/write_pickle_bytes 落盘后 write-through 填入 low 层，立即启用数据可读性。

| 文档 | 变更 |
|------|------|
| CLAUDE.md | object_cache.h low 层描述 CMString→FlyBufferPtr；补 write-through 填入 |
| docs/architecture.md | 读缓存分层表 low 层 CMString→FlyBufferPtr；补 write-through |

改动（13 签名）:
- fly_buffer.h: 新增 FlyBufferPtr 别名
- object_cache.h: put_low/get_low 改 FlyBufferPtr（low 层 std::any 持 shared_ptr）
- data_reader/data_service: read_raw_bytes/try_read_local_raw/read_raw_compressed 等返回 FlyBufferPtr
- database: read_object_compressed 返回 pair<FlyBufferPtr, CMString>
- 回调 typedef + data_client/data_client_pool + worker/master agent: 返回 FlyBufferPtr
- data_server: wire egress FlyBufferPtr→CMString copy（wire 固有）
- write 路径: complete_ lambda 直接传 record（FlyBufferPtr），省 FlyBuffer→CMString copy

消除 copy: write→put_low（2次→0）、read→get_low→返回（1次→0）、DataServer serve 缓存命中（get_low copy→shared_ptr data()）。
保留 copy: wire encode/decode（序列化固有）。

---

## 2026-06-16 (3): 远程读复用 low 层缓存 + hit stats + remove 缓存清理补全

**原因**: ObjectCache low 层存的压缩字节正是远程传输载荷，可用于加速远程 DataServer 服务（省磁盘 IO）。补充 hit stats 诊断 + 修复远程 remove 场景的缓存失效 gap。

| 文档 | 变更 |
|------|------|
| docs/architecture.md | 读缓存分层表 low 层「服务对象」补远程路径（DataServer try_read_local_raw short-circuit）；失效路径补 remove_local_index/remove_remote_index；新增命中统计行 |

改动:
- data_service.cpp try_read_local_raw: 入口 short-circuit（命中 low 层省磁盘 IO）+ 磁盘读后 put_low
- object_cache.h: Stats 结构（per-tier hits/misses/puts/evictions，atomic）+ low_hit_rate/high_hit_rate
- storage_export.cpp: ex_stg_cache_stats Python 绑定
- data_service.cpp remove_local_index/remove_remote_index: 补 ObjectCache::remove（修复远程 remove 广播的陈旧缓存 bug）

---

## 2026-06-16 (2): write API 返回 WriteErrorType 错误码（独立于 TaskErrorType）

**原因**: write_object/write_pickle_bytes 原返回 CMString（成功失败都为空），Python wrapper 靠 task 级累积的 last_error_type 区分成败，导致跨测试 error_type 污染（生产 bug）。改为返回独立 WriteErrorType 错误码，per-call 明确区分。

新增 `WriteErrorType` 枚举（src/common/cpp/error_types.h）：OK/FROZEN_DB/REGISTRATION_FAILED/REGISTRATION_TIMEOUT/DUPLICATE_SKIPPED。不复用 TaskErrorType（那是 task 执行级累积状态，worker_agent task 失败检测依赖）。

| 改动 | 说明 |
|------|------|
| database.h/cpp | write_object/write_pickle_bytes 返回 WriteErrorType（原 CMString）|
| export_macros.h / storage_export.cpp | _write_to_db / _write_pickle_bytes / write_object_raw 返回 int；导出 EXStgWriteErrorType 枚举 |
| database.py | write_object 据 return code 判断（OK/DUPLICATE_SKIPPED = 成功），删除 last_error_type 快照逻辑 |
| last_error_type | 保持 task 级累积语义不变（write_object 仍设它供 worker_agent task 失败检测）|

无文档需更新（Python write_object 签名不变；agent/module.md 的 last_error_type 描述仍准确）。

---

## 2026-06-16: C++ ObjectCache — 两层 LRU 读缓存（low 层下沉 C++ + nanobind high 层）

**原因**: 新增 C++ 侧 read 缓存系统，统一 master/worker 进程的读缓存。low 层（压缩字节）下沉 C++，消除 Python 与 C++ 的双缓存冗余；high 层（反序列化对象）C++ 服务 read_object<T> + nanobind 类（经 _read_from_db），Python 服务 pickle 对象。

| 文档 | 变更 |
|------|------|
| CLAUDE.md | 存储层文件表新增 object_cache.h 行（两层 LRU + std::any + LFU 淘汰） |
| docs/python-api/module.md | read_cache.py 描述更新（low 下沉 C++）；read_object cache 语义补充分层 |

新增文件: `src/storage/cpp/object_cache.h`（header-only，进程级单例）、`src/storage/tests/object_cache_test.cpp`（16 单测）、`qa/test_cpp_object_cache.py`（6 case，含 high 层命中断言）。
集成: `read_object_compressed` low 层（命中省 IO）、`read_object<T>` high 层（命中省反序列化）、`remove_object`/`remove_index_entry` 失效。
nanobind: `FLY_EXPORT_SERIALIZE` 加 `_read_from_db`（对称 `_write_to_db`）；`_get_py_name` 辅助分派；database.py read_object 据类型分流（nanobind→C++ high / pickle→Python high）。
诊断: `ex_stg_cache_high_size` / `ex_stg_cache_clear`（测试/观测用）。

---

## 2026-06-15 (2): connect 失败非致命化 — 返回 0 sentinel，不抛异常

**原因**: 8606397 网络层重构后 `connect()` 失败抛异常，把连接失败当致命错误，破坏 worker_agent_test（无 master 场景）及「连接失败非致命」契约。改为返回 sentinel 让调用层判断。

| 文档 | 变更 |
|------|------|
| CLAUDE.md | 网络层表格 connection_manager.h 补 connect() 失败返回 0 语义 |
| docs/ARCHITECTURE_REVIEW.md | §3.2 从「待修复（建议 throw）」改为「已处理 — 方向调整（返回 sentinel 不抛）」，记录设计决策 |

新行为：`connect()` 失败返回 0（conn_id 从 1 起，0 = 失败），不抛异常；`WorkerAgent::start()` 检测 0 后终止（不进入存活未注册态）。

---

## 2026-06-15: db_id 生成策略重构（UUID v4 → path-hash + 随机）

**原因**: db_id 从 UUID v4（32 hex）改为 10-char base62（4 char path-hash 前缀 + 6 char 随机后缀）。同路径 → 同前缀，使路径迁移后 load 旧 db + 原路径新建的碰撞可被检测。

| 文档 | 变更 |
|------|------|
| docs/python-api/module.md | §open_db 路径检测 db_id 格式说明（UUID v4 → 10-char base62）；db_id 生成段重写（path-hash 前缀 + 随机后缀 + 碰撞检测） |

---

## 2026-06-01: 用户脚本 task 支持 + 两层读缓存

| 文档 | 变更 |
|------|------|
| docs/python-api/module.md | `as_task` 实现描述更新（from_user 协议）；`read_object` 新增 `cache` 参数 |
| qa/README.md | 新增"扁平脚本"编写规范，移除 `__main__`/`main()` 模板 |

新功能：
- **用户脚本 task**: `@as_task()` 装饰器自动检测 `__main__` 模块，将函数 cloudpickle 序列化到 task_name 字段（`from_user` 协议），Worker 端反序列化重建函数执行。用户脚本无需特殊结构。
- **两层读缓存**: `read_object(name, cache="low"|"high"|"none")` — LOW 层缓存压缩数据（避免网络/磁盘 IO），HIGH 层缓存反序列化对象（避免重复 pickle.loads）。LRU + 读取频率淘汰，30s 保护期，1.5x 硬限制。缓存大小由 `read_cache_size` config 控制（默认 1GB）。
- **save_to_db=False**: `write_object(name, obj, save_to_db=False)` 将压缩数据存入 `local_idx_` 的 `temp_compressed_data` 内存字段（不落盘到 DB 文件）。LRU 淘汰溢出到 TempStore 临时文件。`read_object` 通过 `local_idx_` 统一路径透明访问（支持跨 Worker，通过 WriteRegister 通知 master）。`remove_object` 清理 temp 条目。freeze/析构时自动清理。
- **Master 自动启动**: `init()` 中自动调用 `agent.start()`，用户无需手动启动 Master。
- **QA 公共 API 规范**: 所有 QA 测试只使用公共 Python API（`master.worker_count`、`master.wait_for_workers()` 等），底层 C++ 测试迁移至 `qa/internal/`。每个测试文件只测试一个场景。
- **QA 扁平脚本**: 所有 QA 测试移除 `if __name__ == "__main__":`、`main()`、`master.stop()`、`del db`。代码从上到下直接执行。
- **Master 公共 API**: 新增 `worker_count`、`wait_for_workers(n, timeout)`、`is_running()`、`get_worker_pids()`。

---

## 2026-05-31: Code Review Fixes — API 安全性改进

| 文档 | 变更 |
|------|------|
| docs/task/module.md | `get_worker()` / `get_task()` 返回类型从指针改为 `std::optional<std::reference_wrapper<T>>` |
| docs/agent/module.md | `set_data_service(DataService*)` → `set_data_service(CMWeakPtr<DataService>)`；`WorkerAgentContext` 回调从 C 函数指针 + trampoline 改为 `std::function` + lambda；移除所有 trampoline 声明；新增 `set_notify_removed_func`/`set_remove_request_func`/`set_backup_request_func`；`DataService` 继承 `enable_shared_from_this`，新增 `instance_ptr()` |
| docs/storage/module.md | 回调模式从 C 函数指针更新为 `std::function`；`DataService` 继承 `enable_shared_from_this`；设计决策表更新 |
| docs/network/module.md | `register_handler` 优化说明（decode() 已 in-place 修改 buffer，无需额外拷贝） |

核心变更：
- `WorkerManager::get_worker()` 返回 `std::optional<std::reference_wrapper<WorkerInfo>>` 替代 `WorkerInfo*`
- `TaskManager::get_task()` 返回 `std::optional<std::reference_wrapper<TaskMetadata>>` 替代 `TaskMetadata*`
- `WorkerAgentContext` 所有回调改用 `std::function`，移除 `void*` ctx 和全部 7 个 trampoline 静态函数
- `DataService` 继承 `std::enable_shared_from_this<DataService>`，`instance_ptr()` 返回 `CMSharedPtr<DataService>`；Agent 通过 `CMWeakPtr<DataService>` 观察
- `Config::INVALID_INT`（`INT64_MIN`）替代未知 key 抛异常；`get_int()` 使用 `fprintf(stderr, ...)` 日志（core 模块无 log 依赖）
- `TaskExecutor::cancel()` 移除（header、cpp、test、export）
- Reactor `register_handler` 移除冗余 buffer 拷贝（`decode()` 已 in-place 修改）
- `main.py` triple `gc.collect()` → 单次调用

---

## 2026-05-30: 统一流式序列化+压缩管线重构

| 文档 | 变更 |
|------|------|
| docs/storage/module.md | 全面重写：Database API（删除 write_object/write_object_typed/write_object_buffer/read_object 非模板版，新增 write_object_raw_ptr/read_object_compressed）；DataWriter 简化为纯落盘；DataReader 简化为纯读字节；IndexEntry 删除 compression_type 字段和版本控制；写入/读取流程更新为流式管线架构；新增序列化宏说明 |
| CLAUDE.md | FLY_SERIALIZE 说明更新为自动生成 fly_serialize/fly_deserialize |
| AGENTS.md | storage 模块描述更新，新增 CompressingStreamBuf/DecompressingStreamBuf |
| src/main/cpp/BUILD | 所有导出 .so 加入 data 依赖，修复 bazel clean 后 .so 不重建问题 |

核心变更：
- DataWriter 移除所有压缩配置和逻辑，只保留 write_record 纯落盘
- DataReader 移除所有解压逻辑，只保留 read_raw_bytes + exists
- Database 统一管理 compress_data_to_buffer（写）和 DecompressingStreamBuf（读）
- FLY_STREAMABLE() 宏合并进 FLY_SERIALIZE_END，所有 FLY_SERIALIZE 类型自动获得流式能力
- FLY_EXPORT_SERIALIZE 合并 _write_to_db + is_cpp + __getstate__/__setstate__
- write_record 删除 compression_type 参数，IndexEntry 删除 compression_type 字段
- IndexEntry 删除版本控制（FLY_SERIALIZE_BEGIN → FLY_SERIALIZE）
- Python database.py 简化为两条写路径：_write_to_db / pickle
- 删除 write_object(name, data) / read_object(name) / write_object_typed / write_object_buffer

---

## 2026-05-30: backup 数据复制 — 压缩传输零解压落盘

| 文档 | 变更 |
|------|------|
| docs/architecture/overview.md | 数据副本策略从"低/未实现"更新为"已完成：backup=True 压缩传输零解压落盘" |
| docs/python-api/module.md | write_object/read_object/write_object_raw/read_object_raw 签名新增 backup=False 参数说明 |
| docs/storage/module.md | read_object/read_object_typed 签名新增 backup=false；新增 backup_object() 声明 |

---

## 2026-05-25: 优雅关机 + workers_mutex_ 线程安全

| 文档 | 变更 |
|------|------|
| docs/agent/module.md | 关机流程重写为"优雅关机（Graceful Shutdown）"：drain 语义、pending task 持久化、stop 幂等；MasterAgent 成员变量新增 draining_/shutdown_requested_/fatal_error_/workers_mutex_/drain_mutex_/drain_cv_/drain_thread_；WorkerAgent 新增 shutdown_triggered_；schedule_tasks 新增 draining early return；on_disconnect 新增 draining 跳过恢复逻辑；设计决策表新增 workers_mutex_/stop 幂等/SIGTERM Python 层处理 |
| CLAUDE.md | 新增 Agent 工作指南 §7 禁止项：禁止归因为 pre-existing bug、禁止忽略 crash/不稳定性、崩溃与不稳定性零容忍 |

代码变更摘要：
- `master_agent.h/cpp`: stop() 改为 drain 语义（广播 shutdown → 等待 running tasks → persist pending → cleanup）；schedule_tasks() draining early return；on_task_failed 设 fatal_error_；on_disconnect draining 跳过恢复；新增 workers_mutex_ 保护 conn_to_worker_/worker_to_conn_ 全部并发访问（修复 SIGSEGV）；新增 persist_pending_tasks()、build_failed_record()、notify_drain_if_active()、check_shutdown_request()（dead code）、do_drain_and_stop()
- `worker_agent.h/cpp`: initiate_shutdown() 幂等（shutdown_triggered_）；stop() → initiate_shutdown() → do_cleanup()
- `reactor.h`: 新增 is_running()、get_io_pool()
- `data_service.h/cpp`: stop_transfer_server() 中 reset transfer_pool_；新增 reset() 公共方法
- `main.py`: SIGTERM handler → SystemExit(0) → cleanup
- `master_agent_test.cpp`: 4 new tests (StopWithPendingTasks, StopNoRunningTasks, StopIdempotent, StopBeforeStart)
- `worker_agent_test.cpp`: 1 new test (InitiateShutdownFromOnDisconnect)
- qa/: 5 new files (test_graceful_shutdown, test_shutdown_broadcast, test_pending_task_persist + 2 helpers)

---

## 2026-05-25: FlyBuffer 统一 + 流式管线架构重构

| 文档 | 变更 |
|------|------|
| docs/storage/module.md | 写入流程改为"流式管线 + 异步落盘"架构；新增 FlyBufferStreamBuf/CountingStreamBuf 组件描述；DataWriter 新增 compress_to_buffer/write_record |
| CLAUDE.md | 存储层文件表更新（database/data_writer/fly_buffer_stream）；序列化部分新增 FlyBuffer 说明；写入架构约束更新 |

代码变更摘要：
- `fly_buffer.h`: FlyBuffer 内部存储从 `CMVector<uint8_t>` 改为 `CMString`，消除 char↔uint8_t 阻抗失配；新增 `take(CMString&&)` / `release()` 支持零拷贝
- `serialization_macros.h`: FlySerBuf 改为 FlyBuffer 别名；FLY_ENCODE/DECODE 去掉 std::transform 转换；新增 bitsery traits 特化
- `fly_buffer_stream.h`（新建）: FlyBufferStreamBuf（streambuf→FlyBuffer）+ CountingStreamBuf（字节计数）
- `data_writer.h/cpp`: 新增 `compress_to_buffer`（流式管线：FlyBufferStreamBuf→CompressingStreamBuf→FlyBuffer）和 `write_record`（仅 file_stream_.write + index 更新）
- `database.h/cpp`: write_object 模板改为调用线程 serialize+compress → WBQ 仅 write_record；新增 write_object_raw_ptr 接受裸指针
- `export_macros.h`: `__getstate_buffer__` 改用 FLY_ENCODE_TO_BUFFER 直接写入 FlyBuffer
- `storage_export.cpp`: 新增 `_write_pickle_bytes`（Python bytes 裸指针直接进 compress_to_buffer）和 `_write_raw_ptr`
- `database.py`: Python pickle 路径改用 pickle.dumps + _write_pickle_bytes
- `data_reader.cpp`: 读取路径 `FlySerBuf(str.begin(), str.end())` 改为 `take(std::move(str))` 零拷贝

---

## 2026-05-25: DataService 两层索引重构 + 并发 Bug 修复

| 文档 | 变更 |
|------|------|
| docs/storage/module.md | `local_idx_`/`remote_idx_` 类型从 `Map<full_name, ...>` 改为两层嵌套 `Map<db_id, Map<short_name, ...>>`；新增 `split_full()` 定长切分（32 字符 db_id） |

代码变更摘要：
- `data_service.h/cpp`: `local_idx_`/`remote_idx_` 改为两层索引；`split_full()` 使用 32 字符定长切分；`on_flush(db_id)` 优化为 O(该 db 条目)
- `local_index.h/cpp`: 加 `std::mutex` 保护所有公共方法；`save()` 锁内取快照锁外做 I/O（修复 WBQ 线程与主线程数据竞争导致的 std::bad_alloc）
- `tcp_transport.cpp`: `send()` 处理 partial send 和 EAGAIN（poll+retry 循环）
- `worker_agent.cpp`: `on_remove_command` 提取 short name（修复 double-prefix）
- `database.cpp`: `freeze()` 不再关闭 DataWriter（修复 in-flight write 竞争）
- `main.cpp`: 新增 `std::set_terminate()` crash handler + SIGABRT/SIGSEGV handler + backtrace
- 4 个测试文件改为使用 32 字符 db_id

---

## 2026-05-24 (7): Bug 修复 + 压力测试 + freeze 机制完善

| 文档 | 变更 |
|------|------|
| CLAUDE.md | 新增 §QA 测试与 test 模块；新增 §内部接口；test 模块描述；消息结构数量 26→27 |
| qa/README.md | 新增 §6 压力测试（7 覆盖场景 + 2 未覆盖场景） |
| docs/test/module.md | 新增 `increment`、`write_after_freeze` 任务文档 |
| docs/network/module.md | 消息结构数量 26→27；新增 `DatabaseFreezeNotification` |
| docs/architecture.md | 消息结构数量 23→27 |
| docs/architecture/overview.md | 消息类型总览 22→26+header |
| docs/DOC_CHANGELOG.md | 本条记录 |

---

## 2026-05-23 (5): writer_id UUID 解耦 idx/data 文件命名

**原因**: load_db 时 worker_id 与 idx 文件名耦合导致冲突限制，Master 无法在任意机器上重启

| 模块文档 | 主要变更 |
|----------|----------|
| CLAUDE.md | §13 load_db 流程重写（Phase 0 冲突检查移除、Phase 2 不加载 idx、Phase 3 hostname 复用、Phase 4 含 Master writer_id）；§8 MasterAgent/WorkerAgent 模块描述更新；_DB_META WorkerInfo 新增 writer_id 字段 |
| docs/storage/module.md | DataWriter/DataReader 构造函数参数 `uint64_t worker_id` → `const CMString& writer_id`；私有成员 `worker_id_` → `writer_id_` |
| docs/agent/module.md | `restore_master_idx`/`send_idx_load_commands` 签名更新（writer_ids）；load_db 流程重写（Phase 2-5 全部更新） |
| docs/network/module.md | `IdxLoadCommandMessage.old_worker_ids` → `writer_ids` (CMVector\<CMString\>) |

**代码变更摘要**：
- `writer_id`: 8-char hex UUID，Database 构造时生成，用于 idx/data 文件命名
- 文件命名：`{writer_id}.idx`、`data_{writer_id}_{index:03}.dat`（替代 `worker_{id}.idx`、`aggregated_w{id}_*.dat`）
- `DataReadyMessage` 新增 `writer_id` 字段，Worker/Master 均填充
- `IdxLoadCommandMessage.old_worker_ids` → `writer_ids`
- `rebuild_remote_idx`：统一路径，worker_id==0 不再特殊处理，所有条目按 hostname 映射到新 Worker
- Master load_db 时不加载任何 idx 到 local_idx，所有旧数据通过 remote_idx 经 Worker 提供
- `MasterAgent` 新增 `get_worker_hostnames()`、`add_worker_hostname()`
- `register_worker(0, host_, port_)` 在 `start()` 中调用（Master 新数据仍需被 Worker 读取）
- `recorded_workers_` key 从 `pair<hostname, worker_id>` 改为 `tuple<db_id, hostname, writer_id>`
- agent.py load_db 重写：hostname-based worker 复用，Master writer_id 含入 idx load commands
- 新增 3 个多 hostname 单元测试覆盖 idx 分配场景
- 3 个网络测试 flaky 修复（poll loop 替代 sleep+assert）

---

## 2026-05-23 (4): load_db 增强 + 跨 DB QA 测试

**原因**: 连续 load_db 多个 DB 时 `_next_worker_id` 回退导致 worker ID 冲突；load_db 恢复后数据未标记依赖就绪

| 模块文档 | 主要变更 |
|----------|----------|
| CLAUDE.md | §load_db 完整流程增加 Phase 0 冲突检查、Phase 2 mark_data_ready + frozen 恢复、连续 load_db 说明 |

**代码变更摘要**：
- `agent.py` `load_db`: 新增 worker ID 冲突检查（重叠 → RuntimeError）；`_next_worker_id` 取 `max(当前, max(old)+1)` 不回退
- `database.cpp`: 构造函数检测 `_FROZEN` 标记恢复 `is_frozen_` 状态
- `master_agent.cpp`: `recorded_workers_` 从 `pair<hostname,worker_id>` 改为 `tuple<db_id,hostname,worker_id>`（per-DB 记录）；`restore_master_idx` + `rebuild_remote_idx` 新增 `mark_data_ready`
- 新增 6 个跨 DB e2e_tasks：cross_db_copy, cross_db_sum, add_alpha_property, alpha_cross_db_copy, gpu_cross_db_copy, triple_db_sum
- 新增 QA 测试 test_complex_scenario.py：2 进程协调器，覆盖多 DB、跨 DB 依赖、load_db 双 DB 迁移、动态属性、restart_failed_tasks、triple-DB 计算（12 个验证点）

---

## 2026-05-23 (3): 异步写入依赖调度重构

**原因**: `write_object` 异步写入时立即触发依赖满足，移除 `restart_failed_tasks` 中的 `drain_write_back` 同步阻塞

| 模块文档 | 主要变更 |
|----------|----------|
| CLAUDE.md | 重写 §restart_failed_tasks API（简化流程）、重写 §写入注册触发依赖满足（Worker/Master 端分离、线程安全） |
| docs/agent/module.md | WriteRegisterMessage 语义变更（增加 mark_data_ready + update_remote_idx） |

**代码变更摘要**：
- `on_write_register`: 收到 Worker WriteRegisterMessage 后立即 `mark_data_ready` + `update_remote_idx` + `schedule_tasks`
- `setup_write_context`: Master 端新增 `master_register_write_trampoline`，`write_object` 时同步触发 `mark_data_ready`
- `restart_failed_tasks`: 移除 `drain_write_back` + 手动依赖检查，简化为直接 `submit_task`
- `schedule_tasks`: 新增 `schedule_mutex_` 防止 WriteBackQueue 工作线程与 Python 线程并发导致重复 fail/persist

---

## 2026-05-23 (2): db_id UUID v4 + open_db 路径递增

**原因**: db_id 从 hash(base_path) 改为 UUID v4；open_db 检测已有 DB 时自动递增路径

| 模块文档 | 主要变更 |
|----------|----------|
| CLAUDE.md | 新增 §open_db vs load_db 路径检测表（递增路径）、§db_id 生成（UUID v4）、DataService db_paths_ register/unregister 行为 |
| docs/python-api/module.md | 新增 §open_db 路径检测（自动递增 `.1` `.2`... + WARN）、db_id UUID v4 说明 |
| docs/storage/module.md | Database 构造函数注释（UUID v4、析构 unregister）、DataService register_database 严格检查注释 |

---

## 2026-05-23 (1): load_db 文档同步

**原因**: load_db 功能实现完成后，同步更新所有相关模块文档

### 变更汇总

| 模块文档 | 主要变更 |
|----------|----------|
| storage/module.md | Database 构造函数新增 `existing_db_id`，DataService 新增 `has_database()`、`restore_entries()`、`DbPaths` struct |
| agent/module.md | MasterAgent 新增 `restore_master_idx()`、`send_idx_load_commands()`、`rebuild_remote_idx()`、hostname 映射、`on_idx_load_ack()` handler；WorkerAgent 新增 `on_idx_load_command()` handler；新增 load_db 恢复流程文档 |
| network/module.md | 消息类型 24→26 种；RegisterMessage 新增 `hostname`、`ip_address` 字段；新增 `IdxLoadCommandMessage`(type=25)、`IdxLoadAckMessage`(type=26) |
| python-api/module.md | FlyAgent 新增 `load_db()`、`wait_for_all_workers()`；`_deserialize_args` 增加 `has_database` 检查说明；新增 load_db 使用示例 |
| superpowers/plans/2026-05-22-* | DbMetaHeader/DbMeta 移除 `base_path`；Phase 3 改为 process worker；Phase 4 增加 `register_database` 步骤 |
| CLAUDE.md | §8 DataService (db_paths_, has_database)、§13 load_db 流程更新（process worker, base_path 移除） |

---

**日期**: 2026-05-21
**原因**: 文档与实现代码存在大量不一致，本次批量修正

---

## 一、变更汇总

| 模块 | 差异数 | 主要变更 |
|------|--------|----------|
| log | 18 | 架构从多实例改为单例，API完全重写 |
| network | 28 | 新增MasterClient，TransportLayer/Reactor签名变更 |
| task | 40 | WorkerInfo字段重设计，TaskScheduler签名变更 |
| storage | 22 | 写流程改为异步WriteBackQueue，DataReader实例化 |
| agent | 23 | WorkerAgentContext从指针改为回调模式 |
| core | 7 | 新增8个int+6个string配置项 |

---

## 二、详细变更

### 2.1 log/module.md — 架构完全重写

**旧设计（文档）**:
- 多实例模式：`CMMap<CMString, Logger>` 存储多个Logger
- `get_master()`, `get_worker(worker_id)` 分角色获取
- `init_master(path)`, `init_worker(worker_id, path)` 分角色初始化
- `debug(component, msg)` 两参数日志方法
- 日志格式：`[timestamp] [LEVEL] [component] msg`

**新设计（实际代码）**:
- 单例模式：`static Logger* instance_`
- `Logger& instance()` 统一获取
- `init(base_dir, worker_id)` 统一初始化
- `debug(msg)` 单参数日志方法（无component）
- 日志格式：`[timestamp] [LEVEL] msg`
- 日志rotation：`resolve_log_dir()` 创建版本化目录 + `.latest` symlink
- 宏定义：`DBG(msg)`, `INFO(msg)`, `WARN(msg)`, `ERR(msg)`
- Python导出：`init_log`, `shutdown_log`, `flush_log`, `set_log_level`, `DBG/INFO/WARN/ERR`

---

### 2.2 network/module.md

**新增类**:
- `MetadataClient`（原名 `MasterClient`） — 阻塞TCP客户端，查询Master数据位置
  - `query_data_location(host, port, object_name)` → `DataLocation`

**签名变更**:
- `TransportLayer::accept()` → **已移除**，改为 `stop_listening()`
- `TransportLayer::get_bound_port()` 返回 `int` 而非 `int32_t`
- `Reactor` 构造函数：`CMUniquePtr` 而非 `std::unique_ptr`
- `Reactor::set_io_pool()`：`CMSharedPtr` 而非 `IOThreadPool*`
- `Reactor::recv_buffers_`, `handlers_`：`CMUnorderedMap` 而非 `CMMap`
- `IOThreadPool` 构造函数接收 `thread_count`，`start()` 无参数
- `DataClient::request_data()` 返回 `std::tuple<bool, CMString, CMString>` 而非 `DataResponse`

**新增Reactor方法**:
- `on_connect(handler)`, `on_disconnect(handler)`, `on_error(handler)`
- `run_once(timeout_ms)`, `get_bound_port()`, `connect(host, port)`

**消息类型修正**:
- 22种枚举值，但仅17种有对应struct（DATABASE_FREEZE等5种无struct）

---

### 2.3 task/module.md

**DependencyGraph变更**:
- `mark_data_ready()` 返回 `void` 而非 `CMVector<uint64_t>`
- 新增 `get_pending_tasks()`, `is_task_ready()`
- `has_task()` 已移除

**WorkerInfo重设计**:
- 旧：`role` (string), `attributes`, `is_busy` (bool), `last_heartbeat` (double)
- 新：`address`, `port`, `capabilities`, `status` (WorkerStatus enum), `last_heartbeat` (uint64_t)
- WorkerStatus枚举：`IDLE=0, BUSY=1, DEAD=2`

**WorkerManager签名变更**:
- `register_worker(id, address, port, capabilities)` 而非 `register_worker(id, role, attributes)`
- 新增：`unregister_worker()`, `update_worker_status()`, `assign_task()`, `get_workers_with_capability()`
- 移除：`has_worker()`, `get_available_workers()`, `get_all_worker_ids()`, `update_attributes()`

**TaskScheduler变更**:
- 构造函数：`DependencyGraph*`, `WorkerManager*` 原始指针而非shared_ptr
- `submit_task()` 已移除（任务提交在MasterAgent层）
- `schedule_next()` → `ScheduleResult`
- `schedule_all_available()` → `CMVector<ScheduleResult>`
- 新增 `ScheduleResult` 结构：`{task_id, worker_id, scheduled}`

**MetadataManager变更**:
- TaskStatus枚举：`PENDING=0, RUNNING=1, COMPLETED=2, FAILED=3, CANCELLED=4`（无READY）
- `create_task(id, name, inputs, outputs, config)` 新增outputs/config参数
- `get_task()` 返回指针而非值
- 新增字段：`outputs`, `config`, `created_at`, `started_at`, `completed_at`, `error_message`, `assigned_worker_id`

**HeartbeatMonitor变更**:
- 构造函数：`WorkerManager*` 原始指针 + `timeout` 参数
- 默认timeout：30秒而非120秒
- `check_all_workers(uint64_t)` 而非 `check_all_workers(double)`
- `set_timeout(uint64_t)` 而非 `set_timeout(double)`

---

### 2.4 storage/module.md

**Database变更**:
- 构造函数：`writer_id` 类型 `uint64_t` 而非 `int`，新增 `host` 参数
- `flush()` 不再作为公共方法（异步WriteBackQueue）
- 新增：`load_meta()`, `set_db_id()`, `reset()`
- 写流程：异步入队 `WriteBackQueue`，非阻塞返回

**DataWriter变更**:
- 构造函数：11个参数而非2个
- 新增：`close()`, `total_bytes_written()`, `file_count()`
- `get_last_entry(object_name)` 返回指针，需object_name参数

**DataReader变更**:
- 所有方法从 `static` 改为实例方法
- 构造函数：`DataReader(base_path, data_path, worker_id)`
- 新增：`exists()`, `read_object<T>()`

**DataService变更**:
- `try_read_local()` 返回 `std::pair<bool, ReadResult>` 而非 `ReadResult`
- `lookup_remote_idx()` 返回 `RemoteObjectInfo` 结构而非输出参数
- 新增：`register_database()`, `unregister_database()`, `has_local_object()`, `has_remote_location()`
- 新增：`start_transfer_server()`, `stop_transfer_server()`, `drain_write_back()`
- 移除：`process_completions()`

**IndexEntry变更**:
- 版本：3而非2
- `block_count`：`int32_t` 而非 `int`
- `compression_type`：`int8_t` 而非 `CompressionType` 枚举
- 新增 `host` 字段

**Compressor变更**:
- 从静态工具类改为虚接口
- 实例方法：`compress()`, `decompress()`, `compress_chunk()`, `decompress_chunk()`
- 工厂方法：`create()`, `create_from_name()`

---

### 2.5 agent/module.md

**WorkerAgentContext重构**:
- 旧：`set(WorkerAgent*)` 指针存储 + `current()` 获取
- 新：`set(RecordWriteFunc, void* ctx)` 函数指针回调 + `record_write()` 调用回调
- 文件位置：`src/common/cpp/worker_context.h` 而非 `src/agent/cpp/`

**MasterAgent变更**:
- `get_port()`：`uint16_t` 而非 `int`
- `get_pending/running/completed_tasks()`：返回 `CMVector<uint64_t>` 而非 `int`
- 新增：`set_data_service()`, `get_connected_workers()`, `register_database()`, `is_db_frozen()`, `request_remote_data()`, `request_data_from_worker()`
- 移除：`on_data_query()`（改为 `on_data_ready()`）

**WorkerAgent变更**:
- `poll_task()`：返回 `bool` 而非 `void`
- `request_data_from_worker()`, `request_remote_data()`：返回 `ReadResult` 而非 `DataResponse`
- 新增：`get_worker_id()`, `set_executor()`, `begin_task()`, `record_write()`, `end_task()`, `register_write_with_master()`
- 消息处理器：移除 `conn_id` 参数
- 移除：`on_data_location()` 处理器

**TaskExecutor变更**:
- TaskExecStatus枚举新增 `TIMEOUT=2`
- 新增：`clear_exec_func()`, `is_running()`

---

### 2.6 core/module.md

**新增int配置项**:
- `worker_mode`, `worker_id`, `compression_level`, `compression_threshold`, `compression_stream_chunk_size`, `dependency_update_mode`, `interactive`, `cli_master_port`

**新增string配置项**:
- `transport_type`, `compression_type`, `data_server_host`, `master_host`, `log_dir`, `script_path`

**值修正**:
- `large_file_threshold`：已修正为 `67108864`（64MB），新增 `large_file_threshold_kb`（65536，用户可配置 KB 单位）
- `database.cpp` 使用 `large_file_threshold_kb * 1024` 计算字节阈值

**Python导出修正**:
- 移除 `FLY_EXPORT_INIT()`（实际不存在）
- 新增：`mark_workers_launched`, `is_workers_launched`, `reset`
- `ex_core_get_config`：返回指针而非引用

---

## 三、文档修正状态

| 文档 | 状态 |
|------|------|
| log/module.md | 已修正 |
| storage/module.md | 已修正 |
| network/module.md | 已修正 |
| task/module.md | 已修正 |
| agent/module.md | 已修正 |
| core/module.md | 已修正 |
| architecture/overview.md | 已修正 |
| DEVELOPMENT_GUIDELINES.md | 无需修正 |
| superpowers/plans/*.md | 保留历史记录，不修正 |

---

## 四、代码修正建议

| 位置 | 问题 | 建议 |
|------|------|------|
| `config.cpp:66` | `large_file_threshold = 10485710` | **已修正**: 改为 64MB (`67108864`)，新增 `large_file_threshold_kb = 65536`，database.cpp 使用 `large_file_threshold_kb * 1024` |
| `master_client.h/cpp` | `MasterClient` 命名不准确 | **已修正**: 重命名为 `MetadataClient`，功能为元数据查询 |
---

## 2026-07-02/03 代码瘦身与重复消除重构

### 一、死代码与冗余抽象清理（commit 534510f）

基于 `docs/redundancy-audit-report.md` 三轮复核（静态 grep + 运行时覆盖率 + 设计文档交叉验证），清理 7 项确认无生产消费方的代码：

**真无用代码（FNDA:0 + grep 0 调用）**：
- `ras_graph.py` 删 4 个死导入（保留 `ex_slv_ras_bupdated_solve`）
- `ConnectionManager::recv` 接口+实现、`MessageProtocol::decode_header`、`Reactor::set_handler_pool`/`get_handler_pool`/`handler_pool_` 成员（保留 HandlerThreadPool 类，文档排期阶段 2）
- serialization 删 8 个旧字段宏（`FLY_STR/FLY_VEC/FLY_MAP/FLY_OBJ/FLY_BOOL` 等）+ `FlyTrustedConfig` 冗余字段 + `text_u16/text_u32/map` helper
- export_macros 删 `FLY_EXPORT_STATIC_METHOD`/`FLY_EXPORT_PROPERTY`

**被替代/降级代码**：
- ReadCache low tier（被 C++ ObjectCache 取代），LFU 算法测试重构为走 high tier
- `compress_chunk`/`decompress_chunk` + 整个 `compression_utils` 子系统（生产 0 调用，roadmap F4 降级）
- `StorageManager::get_writer` + `writers_`（生产不可达的孤立闭环）

**文档同步**：
- `serialization/module.md`、`DEVELOPMENT_GUIDELINES.md`：删除已移除旧字段宏的描述，改为"FLY_FIELD 是唯一字段宏"
- `coverage-testing.md`：删除 `compression_utils.cpp` 条目

### 二、重复代码消除（commit 82ac5b3）

消除 5 项重复代码（跳过项6 C++/Python BFS——参考实现 vs 生产实现有意独立）：

- **BE32 解析统一**：`message_protocol.h` 抽 `read_be32`/`write_be32`，替换 10 处手写大端解析 + 2 处写入
- **recv_exact 抽取**：`transport_interface.h` 加共享 `recv_exact`，消除 data_client/data_client_pool(4处)/metadata_client(2处) 的内联循环
- **fetch_from_worker**：新建 `agent/cpp/data_fetch.h`，master/worker 的 `request_data_from_worker` 委托
- **PendingRpcMap 模板**：新建 `agent/cpp/pending_rpc_map.h`，5 套 Pending（DbPath/WriteRegister/Freeze/VarOp/Remove）迁移到模板实例，消除 15 个 mutex/cv/map 成员；PendingRemove 双锁收敛为单锁
- **coarse grid 去重**：`ras_graph.py` 抽 `_compute_coarse_arrays`，`_build_coarse_operators` 与 legacy fallback 共享

**文档同步**：
- `CLAUDE.md`：transport_interface 补 `recv_exact`、message_protocol 补 `read_be32/write_be32`、agent 层补 `pending_rpc_map.h`/`data_fetch.h`

### Python 依赖 hermetic 化 + 构建清理（本轮）

**问题根因**：Python 第三方包（cloudpickle/numpy/scipy/pytest）此前靠系统 site-packages 隐式解析——`.bazelrc` 硬编码 `PYTHONPATH=/usr/local/lib/python3.12/site-packages` 给 py_test，生产 `fly`（嵌入 libpython）靠系统 `/usr/lib/python3.12/site-packages`。换机器或缺包即崩（曾导致 QA 44 个测试失败、user_task_test 6/15 失败）。

**方案 B-重：双路径 hermetic**

- `MODULE.bazel` 加 `pip.parse` 扩展（rules_python），从 `requirements_lock.txt`（pip-compile 从 `requirements.in` 生成）拉取 wheel
- 5 个 `py_library`/`py_test` 的 BUILD 加 `load("@pip//:requirements.bzl", "requirement")` + 对应 `requirement("<pkg>")`：
  - `src/agent/py`（executor.py）、`src/task/py`（task.py）、`src/fly`（mapreduce.py）→ cloudpickle
  - `src/solver/py`（ras_graph.py）→ numpy + scipy
  - `src/fly/tests:user_task_test` → cloudpickle
- `fly.sh do_install` 新增段：用 `bazel cquery @pip//<pkg>:extracted_whl_files` 定位 wheel 解压路径，`cp -r` 复制到 `build/python/lib/python3.12/site-packages/`（cloudpickle+numpy+scipy，约 168M；pytest 不复制，仅 py_test 沙箱用）
- `src/main/cpp/main.cpp::setup_sys_path()` 在 build 布局加一行 `sys.path.insert` 注入 site-packages（含存在性检查，无 install 时静默跳过）
- `.bazelrc` 删硬编码 `PYTHONPATH`（改由 `requirement()` 显式声明）

**验证**（系统卸载 cloudpickle 的最严苛条件）：C++ 单测 50/50，QA 106/106 真实通过，py_test 与生产 fly 双路径均从 bazel wheel 加载。

**架构边界澄清**：`fly` 是 cc_binary 嵌入 `libpython3.12.so`，依赖"动态库 + importable Python 模块"，**不依赖 python3 可执行解释器**（grep 确认无 subprocess/exec python）。libpython 本身仍为系统依赖（同 libc 级别），完全 hermetic 化它需切 rules_python hermetic libpython，不在本轮范围。

**规则版本**：`MODULE.bazel` `rules_python` 1.2.0 → 1.7.0（实际因传递依赖早解析到 1.7.0，声明同步消除版本不匹配警告）。

**僵尸文件清理**：
- 删 `qa/BUILD`（693 行，39 个 py_test 全部引用 `3e59ff6` QA 目录重构后不存在的顶层 `qa/*.py`，无任何消费者，`fly.sh` 只用 `//src/...`；QA 实际入口是 `qa/runqa`）
- `git rm --cached` 两个误提交产物 `qa/var/persistence_db/_VARS`、`qa/var/test_var_freeze/db/_VARS`
- `.gitignore` 补 `qa/*/test_*/`（覆盖无数字后缀产物目录）、`_VARS`、清理游离 `-e ` 行

**死变量清理**：`master_agent.cpp::on_database_freeze_request` 删 `should_broadcast`（stream 分支赋值后从未读取，广播逻辑实际用 `accepted && streaming_mode` 判断，等价无副作用）。

**文档同步**：
- `CLAUDE.md`：构建区加"Python 第三方依赖"段，说明 hermetic pip + 新增包流程
- `DEVELOPMENT_GUIDELINES.md 7.1`：`.bazelrc` 配置同步实际内容，删 stale 的 `--enable_bzlmod=false`，加"不要用 PYTHONPATH action_env"警示
- 新增 `requirements.in`（高层规格）、`requirements_lock.txt`（pin 版本锁）、`tools/BUILD`（聚合 `fly_third_party_py` filegroup）
