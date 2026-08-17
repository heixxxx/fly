# Fly 未做项清单

> 基于 2026-08-12 文档 vs 代码全面对比（3 个 agent 调查），更新于 2026-08-16
> 状态：❌ 未做 / 🟡 部分 / ✅ 已做 / 🔄 进行中 / ⛔ 明确不做

---

## 一、核心功能空缺

| 编号 | 项 | 状态 | 说明 |
|---|---|---|---|
| **F3** | Worker role（storage_only/hybrid） | ✅ | 已实现（2026-08-15）：role 静态身份（注册时设定不可变更），调度候选层过滤 storage_only（get_idle_workers，scheduler 零 role 概念）；仍参与心跳/数据面/internal 数据 task |
| F1 | SSH / 多机 Worker 启动 | ⏸ 降级 | 功能层缺，待多机测试环境 |
| F2 | Freeze 后处理（idx 合并/merged.idx/_META 聚合） | ⏸ 降级 | master 无 IdxRequest handler，database.cpp:440 有 TODO |
| F4 | 大对象分片传输 + 背压 | ⏸ 降级 | DataResponse 两段式不分片，无 credit 流控 |
| — | 弹性 worker（运行时动态加入/退出） | ❌ | 仅常规 register_worker |
| F6 | stage checkpoint / 断点续跑 | ⛔ 不做 | db 级 checkpoint 足够（用户决策）|
| F7 | 协议版本号 | ⛔ 不做 | 早期无需（用户决策）|

---

## 二、存储层

| 编号 | 项 | 状态 | 说明 |
|---|---|---|---|
| **S3** | write_provenance_ 健壮性 | ✅ 已做 | 嵌套map + 时间戳填空hash + load重建 + freeze清理 + merge不继承 + master remove bug 修复（2026-08-12 push）|
| S1-2 | per-object mutex/cv 死代码 | ✅ 已清 | 实施时确认已清理 + atomic |
| **S1-3/M1** | remote_idx_ / write_provenance_ 内存上限（LRU/TTL） | 🟡 部分 | provenance 现在 freeze 清理 + remove 清理（本轮改善）；但仍无数量上限/LRU；remote_idx_ 仍无淘汰；触发阈值 >100万对象 |
| **S4** | TIER1 INCOMPLETE/FAILED 区分 | ✅ 关闭 | INCOMPLETE 本地等待快路径已做（per-db cv）；FAILED 部分经 2026-08-16 复核**非缺陷**（FAILED 锁内瞬时不可见 + 读旧副本语义正确 + can_still_produce 兜底闭环，按原方向修反而破坏正确性；既有测试锁定行为）。遗留 error_message_ 死字段归死代码清理 |
| decay_remote_access 接线 | ✅ 已清 | auto_backup 双层重设计落地（worker TIER2 读流量 suggest + master EWMA 聚合判定）；旧 decay_remote_access/decay_after_backup/evaluate_auto_backup 全链死代码已删除（2026-08-16） |

---

## 三、网络层架构债（源自 2026-06 架构审查，报告已删）

| 项 | 状态 | 说明 |
|---|---|---|
| HandlerThreadPool 未接线（P1）| ✅ 已做 | handler lane 并行分发（同连接保序/跨连接并行，commit 8a7e8b8）|
| Reactor send 同步阻塞 | ✅ 已做 | reactor 路径已非阻塞化（write_buffers + EV_WRITE drain）|
| 背压 / 流控 | ❌ | 无 credit/window |
| DataResponse 大消息分片（=F4）| ❌ | 无 DATA_CHUNK |
| 远程读重试无指数退避 | ✅ 已做 | TIER2（DataService::read_raw_compressed）已有指数退避（10ms×2 上限 500ms + 抖动 + 30s deadline）；request_remote_data 现为 TIER3 纯位置查询不取数不重试 |
| DataClientPool release 不验 fd | ✅ 已做 | keep-alive 连接池改造（commit a408523）：borrow 时 SO_ERROR 预检 + release healthy 标记 + 60s idle TTL |
| Config set_int/set_str 无锁 | ✅ 已做 | mutex 保护全部 get/set（commit 6c82ec9），get_str 改 by value |
| WriteBackQueue 单 worker 线程 | ❌ | 写入吞吐硬瓶颈 |
| conn_send_mutex_map_mutex_ 全局锁 | ✅ 已做 | per-conn send mutex 改 shared_ptr 保活（commit 8a7e8b8）|

> 2026-08-16 移除两项（用户裁定）：
> - **WriteBackQueue 单 worker 线程**——不再作为待办（写序由 WBQ 单线程保证是设计约束，见 S5/S7 设计说明）。
> - **MessageHeader message_id_/timestamp_ 冗余字段**——二次检查确认保留：字段当前虽零使用，但它是全部 33 种消息共用协议头的标准槽位（vlq 编码下 0 值仅占 1 字节/字段），删除需改动全量 wire format，收益近零；保留为未来请求关联/延迟测量的既有槽位。

### 新增已完成（2026-08-13/14）

| 项 | commit | 说明 |
|---|---|---|
| DataServer::stop lost wakeup | 8419526 | send_cv notify 持 send_mutex_，防 send_loop 永久 wait → stop join hang（master_agent_test 偶发挂死的根因，gdb 栈定位）|
| bazel sandbox 截断 src/*/__init__.py | 684eb8e | 根因：sandbox hardlink + bazel runfiles O_TRUNC；test 路径 standalone 根治 |

---

## 四、Solver（预研/可选）

> 2026-08-16 逐项复核完毕（对照 docs/solver/optimization-roadmap.md 2026-08-04 决策），全部为「记录在案、条件未触发」或「有意保留」，无本阶段待办：

| 项 | 状态 | 说明 |
|---|---|---|
| PCG 求解器 | ⏸ 待触发 | optimization-roadmap 定位「⚠️ 可行但有硬瓶颈」：无轻量 Allreduce 原语，每归约 ~70ms，通信占 93%；预估依赖 AMG（fly 实现复杂）。触发条件：reduce 原语补齐 + 迭代重构收益见顶 |
| master 侧轻量 reduce RPC | ⏸ 待触发 | PCG 前置依赖。原「方向 3」已被「方向 2 迭代重构」（PeerChannelGroup RPC，**已实施**）取代——通信开销问题已由直连 RPC 解决；reduce 原语仅在 PCG 立项时一并做 |
| 树形归约 | ⛔ 明确不做 | optimization-roadmap §五已裁定：fly 树形是「伪 O(log nsd)」，每步配对常数项比 MPI 差 100x，master 中心化更优（原「nsd≥16 再做」的说法已被此决策取代） |
| Worker 进程复用（~1.9s 启动） | ⏸ 待触发 | optimization-roadmap 方向 1 记录在案：「风险/收益比不划算」，初始化大头是 BFS（633ms）+ LDLT（1171ms）算法固有成本；方向 2（常驻 daemon task）已消除调度间隙，单次 solve 中启动占比进一步下降 |
| 增量 residual | ✅ 已清 | `residual_cached` 空壳已删（2026-08-16）：现行设计为每步全量精确计算 r = b - A·x（保证数值正确性，避免浮点误差累积），实现增量缓存反而违背设计决策 |
| v1 task 链 | ⚪ 有意保留 | v1（solve_ras_graph，每轮 task 调度 + DB 通信）是 QA golden 正确性基准（golden_solver/test_ras_graph/verify_2d_partition/bench_omega_sweep）与 big_qa scaling 对照链；v2（solve_ras_graph_v2 daemon 常驻 + RPC 直连）是性能主链。双链分工明确非冗余 |

> ⛔ 已否决（正确不做）：GPU 稀疏直接法 / AmgX/Hypre/PETSc / MPI 树形 Allreduce / 全局 LU / 矩阵分块存储（详见 optimization-roadmap §五）

---

## 五、并发与错误处理

| 编号 | 项 | 状态 | 说明 |
|---|---|---|---|
| **P1-8** | write-back lambda 无错误处理 | ✅ 已做 | execute lambda 返回 bool（`write_record_checked` + `flush_checked`）+ WBQ worker_loop try-catch + 落盘失败按错误类型重试/退出；详见 ISSUES.md P1-8 |
| issue 002 | throw→error code 残留 | ✅ 已做 | 核心残留清零（2026-08-17）：config set_int/set_str→bool 哨兵、tcp listen→bool/工厂→nullptr/构造不再 throw、object_header deserialize→bool+输出参数（注：此前文档误记"object_header 已清零"，实为 4 处仍在，本批一并处理）；FLY_DECODE 三宏 throw **保留为受控设计**（所有 decode 入口统一 catch + reactor X-3 清 buffer），2 个真实暴露点已补（database.cpp _DB_META header 局部 catch、export __setstate__ 4 分支转 value_error）。边缘 throw 按惯例保留：export 参数校验 type_error（binding 惯例）、writer_pref_rwlock std::system_error（锁原语惯例）、solver/worker_agent 少量启动期错误 |
| WRITE_REGISTRATION_FAILED | ✅ 已启用 | 2026-08-17 四落点：①空 hash 到达 master（原误标 PROVENANCE_MISMATCH）②未注册窗口防御超时→TIMEOUT（与已注册分支对称）③worker 终止批量 fail pending 写注册（原 UNKNOWN）④master 自写 running_=false（原 {\"\",UNKNOWN} 被当成功放行）。database.cpp 补映射分支（撤缓存+on_write_failed→REGISTRATION_FAILED），error_types.h 注明新语义（原设想「对象已存在拒绝」已被 DUPLICATE_SKIPPED+provenance 取代） |
| **P3-17** | 并发压力测试 | 🟡 | 有 DataService bench + latch 竞态测试，非全结构覆盖 |
| **P3-19** | MetadataClient e2e 测试 | ✅ 已做 | mock master server e2e 已补（metadata_client_test.cpp：多副本成功路径/`can_still_produce_` 透传/server 回 success_=false 路径/往返一致性）；详见 ISSUES.md |

---

## 六、死代码 / 冗余清理

> 2026-08-16 批次清理完毕（逐项先核实"是否真死"再删，详见 DOC_CHANGELOG）：

| 项 | 状态 |
|---|---|
| IOThreadPool 整类 + export + 单测 | ✅ 已清（C++ 生产零使用；EXNetIOThreadPool 仅绑定自测引用，全链删除） |
| GMRES 向量算子 ex_slv_vec_*（7 个）+ ORAS 变体（export + C++ 本体） | ✅ 已清（Python/qa/big_qa 零引用） |
| decay_remote_access + decay_after_backup + evaluate_auto_backup + BackupDecision | ✅ 已清（auto_backup 双层重设计后全链死：worker suggest + master EWMA 已取代；核实新发现 evaluate_auto_backup 也仅测试引用） |
| 死配置键 backup_threshold / backup_replicas / backup_decay_interval / backup_decay_factor | ✅ 已清（全仓零消费者，docs/core/module.md 同步） |
| temp_objects_ + Database::mark_temp + export + Python 调用 | ✅ 已清（集合零读取；is_temp 权威源在 local_idx） |
| LocalObjectInfo.error_message_ | ✅ 已清（写入后条目立即 erase 无人读；on_write_failed 的 reason 参数改为 DBG 日志输出保留诊断价值） |
| IDX_REQUEST/RESPONSE 死枚举 | ✅ 已清（15/16 空号保留注释，不改既有 wire 值） |
| BE32 解析重复 | ✅ 已做（2026-07 commit 82acfd 系：read_be32/write_be32 抽公共，此前清单未更新） |
| removed_objects_ | ⚪ 非死代码（原清单误判）：remove 登记 + freeze 报告 removed_count 活跃，与 compaction TODO 关联，保留 |
| 超长函数未重构 | ✅ 已做 | 2026-08-17/18 三函数收口：schedule_tasks（144→~75 行编排 + compute_locality_hints/fail_and_persist_tasks 两 helper；**assign 必须留锁内**——出锁会让判死检测在「已决策未登记」窗口误杀 pending 链，solver 3 case 实测，回归测试钉住）；read_raw_compressed（149→~45 行编排 + read_tier1_hit/try_tier2_read 两 helper，补 TIER3 回环 2 单测）；merge_db Python（272→~180 行 + _ensure_merge_workers/_delete_merge_source_with_retry 两 helper）。read_raw_compressed 与 try_read_local_raw 的二次索引查询保留（封装进 helper 带注释：后者是 DataServer 热路径共有函数，扩签名回归风险大于收益） |

---

## 七、文档与代码不一致

> 本节所列不一致已全部处理（2026-08-16 批次同步）：ISSUES.md X-7/issue 007 均已标 FIXED；docs/issues/005 已标「已实施」；roadmap S1-2 本轮改 ✅；architecture.md §「尚未实现」列表本轮修正（移除 Locality/Worker 失败恢复/Worker role，页脚日期更新）。

---

## 2026-08-15/16 新增完成（此前未入清单）

| 项 | commit | 说明 |
|---|---|---|
| 存储面 H1 | 2020124 | backup 目标三级 key：host 故障域隔离 → storage_only 优先 → 磁盘水位最轻 |
| 存储面 H2 | 5dfc5e3 | 副本遍历排序：存活 storage_only > 存活 hybrid > 死 holder 排尾 |
| 存储面 H3 | fbb9bc4 | 判死后同 host storage_only 只读接管读服务（recorded_workers_/_DB_META） |
| 存储面 H4 | ee190da | master 自动补齐存储节点（STORAGE_SPAWN）+ 判死提醒（AGENT::0006）+ 重复注册防护（WORKER_PROBE） |
| 50 轮稳定性四并发缺陷 | 5651b09 | 50/50 全过 |
| 注册时序语义收口 | cbbb3fc | 写注册 pending 阻塞 + Ack 先于调度可见 |
| 断连消息语义统一 | 19d9afb | A 类同步 RPC 挂起重放 + B 类入队重放 + TaskSubmitAck 强语义 |
| Logger leak-on-exit / 自动 flush | 5058f01 / c119b1b | P3-18/P3-19 置 FIXED（见 ISSUES.md） |

---

## 本轮已完成（不在原始清单，或原始清单已做）

| 项 | commit | 说明 |
|---|---|---|
| write_provenance_ 生命周期治理 | 1bdf244 | Part A-D + master remove bug + 8 TDD 测试 |
| runqa .pyt 机制 | 多 commit | _run_fly/run_subcase/run_pyt/expect_pass/双发现/thread_local + 140 单进程 + 8 复合 wrapper 全转 .pyt |
| decay_after_backup 接线 | fd24481 | backup 触发后衰减（事件驱动）|
| auto_backup 双层重设计 | ✅ 已完成 | worker suggest（worker_agent）+ master EWMA 聚合（on_worker_backup_suggest）全链落地 |

---

## 建议优先级

1. ~~文档同步~~ ✅ 已完成（2026-08-16 批次：ISSUES/roadmap/architecture/DOC_CHANGELOG/本清单）
2. ~~S4 后半：TIER1 FAILED 读快速失败~~ ✅ 已关闭（2026-08-16 复核非缺陷，见 §二）
3. ~~死代码清理~~ ✅ 已完成（2026-08-16 批次，见 §六；全量单测 56/56 + QA 162/162）
4. ~~Solver 全家~~ ✅ 复核完毕（2026-08-16，见 §四：1 清理 + 1 有意保留 + 4 待触发/不做，无遗留代码欠账）

> 以上四项全部完结。剩余真实欠账：throw→error code 残留 9 处（§五）、WRITE_REGISTRATION_FAILED 未启用（§五）、P3-17 并发测试覆盖面（§五）、超长函数重构（§六）、M1/S1-3 内存上限（待触发）、F1/F2/F4（降级待环境）。
