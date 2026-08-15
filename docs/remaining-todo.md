# Fly 未做项清单

> 基于 2026-08-12 文档 vs 代码全面对比（3 个 agent 调查），更新于 2026-08-13
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
| **S4** | TIER1 INCOMPLETE/FAILED 区分 | 🟡 部分 | 本地写完成→唤醒读者快路径已做；FAILED 仍无差别回退 TIER2 未做 |
| decay_remote_access 接线 | 🔄 进行中 | decay_after_backup 已接线（事件驱动），但 auto_backup 正在重设计为 worker suggest + master EWMA 聚合（详见 `.work/auto_backup_handoff.md`）。旧 decay_remote_access（全量扫描）仍 dead |

---

## 三、网络层架构债（ARCHITECTURE_REVIEW）

| 项 | 状态 | 说明 |
|---|---|---|
| HandlerThreadPool 未接线（P1）| ✅ 已做 | handler lane 并行分发（同连接保序/跨连接并行，commit 8a7e8b8）|
| Reactor send 同步阻塞 | ✅ 已做 | reactor 路径已非阻塞化（write_buffers + EV_WRITE drain）|
| 背压 / 流控 | ❌ | 无 credit/window |
| DataResponse 大消息分片（=F4）| ❌ | 无 DATA_CHUNK |
| 远程读重试无指数退避 | ✅ 已做 | TIER2（DataService::read_raw_compressed）已有指数退避（10ms×2 上限 500ms + 抖动 + 30s deadline）；request_remote_data 现为 TIER3 纯位置查询不取数不重试 |
| MessageHeader 冗余字段（message_id/timestamp 恒0）| ❌ | 未清理（参与 wire format，删除破坏协议兼容）|
| DataClientPool release 不验 fd | ✅ 已做 | keep-alive 连接池改造（commit a408523）：borrow 时 SO_ERROR 预检 + release healthy 标记 + 60s idle TTL |
| Config set_int/set_str 无锁 | ✅ 已做 | mutex 保护全部 get/set（commit 6c82ec9），get_str 改 by value |
| WriteBackQueue 单 worker 线程 | ❌ | 写入吞吐硬瓶颈 |
| conn_send_mutex_map_mutex_ 全局锁 | ✅ 已做 | per-conn send mutex 改 shared_ptr 保活（commit 8a7e8b8）|

### 新增已完成（2026-08-13/14）

| 项 | commit | 说明 |
|---|---|---|
| DataServer::stop lost wakeup | 8419526 | send_cv notify 持 send_mutex_，防 send_loop 永久 wait → stop join hang（master_agent_test 偶发挂死的根因，gdb 栈定位）|
| bazel sandbox 截断 src/*/__init__.py | 684eb8e | 根因：sandbox hardlink + bazel runfiles O_TRUNC；test 路径 standalone 根治 |

---

## 四、Solver（预研/可选）

| 项 | 状态 | 说明 |
|---|---|---|
| PCG 求解器 | ❌ | 全树零 pcg/conjugate_gradient；文档定位"可选备选"|
| master 侧轻量 reduce RPC | ❌ | 全树零 allreduce；PCG 前置依赖 |
| 树形归约（nsd≥16）| ❌ | check 仍单点串行；文档「nsd≥16 再做」|
| Worker 进程复用（消除~1.9s启动）| ❌ | 每次 solve 仍 launch_local_workers 新建 |
| 增量 residual | ❌ | ras_graph_daemon.py:312 声明 residual_cached 但从未赋值——脚手架空壳 |
| v1 task 链清理 | 🟡 | ras_graph.py 仍保留 v1，golden_solver/test 仍用（双路径并存）|

> ⛔ 已否决（正确不做）：GPU 稀疏直接法 / AmgX/Hypre/PETSc / MPI 树形 Allreduce / 全局 LU / 矩阵分块存储

---

## 五、并发与错误处理

| 编号 | 项 | 状态 | 说明 |
|---|---|---|---|
| **P1-8** | write-back lambda 无错误处理 | ❌ | database.cpp:204-223 execute/complete lambda void 返回、无错误处理 |
| issue 002 | throw→error code 残留 | 🟡 | P0 已修，但残留 ~12 处 throw：config.cpp(2)/tcp_connection_manager.cpp(4)/object_header.cpp(4)/FLY_DECODE 宏(2) |
| WRITE_REGISTRATION_FAILED | 🟡 | error_types.h 定义+export 但生产零使用（Part A 后裸写入受时间戳保护，该错误码仍未产生）|
| **P3-17** | 并发压力测试 | 🟡 | 有 DataService bench + latch 竞态测试，非全结构覆盖 |
| **P3-19** | MetadataClient e2e 测试 | ❌ | 仍只有失败用例 + 编解码 |

---

## 六、死代码 / 冗余清理

| 项 | 状态 |
|---|---|
| IOThreadPool 整类死代码 | ❌ network/cpp/io_thread_pool.{h,cpp} 仍在 |
| _MIGRATED_TO 机制 ~130 行（注释自述废弃）| ❌ master_agent/database/data_service/db_meta 仍含 |
| GMRES 向量算子（vec_norm/dot…）+ ORAS 变体 | ❌ solver_export.cpp 7 个 ex_slv_vec_* |
| temp_objects_/removed_objects_ 死字段 | ❌ database.h:194-195；database.cpp:440 TODO |
| IDX_REQUEST/RESPONSE 死枚举无注释 | ❌ message_types.h:26-27 |
| BE32 解析重复未抽公共函数 | ❌ |
| 超长函数未重构 | ❌ read_raw_compressed 155行 / merge_db 234行 / schedule_tasks 121行 |

---

## 七、文档与代码不一致

| 文档 | 不一致点 |
|---|---|
| ISSUES.md | X-7 标 PENDING（实际已修：get_int 返回 INVALID_INT）；issue 007 标 OPEN（实际已全修）|
| docs/issues/005 | 标「待实施」，实际三阶段重构已全部落地 |
| docs/roadmap.md | S1-2 标 🟡 待优化，实际死路径已完全清理+atomic |
| docs/architecture.md | §八「尚未实现」仍列 Locality（已实现）；§5.3 freeze 后处理描述未实现 |

---

## 本轮已完成（不在原始清单，或原始清单已做）

| 项 | commit | 说明 |
|---|---|---|
| write_provenance_ 生命周期治理 | 1bdf244 | Part A-D + master remove bug + 8 TDD 测试 |
| runqa .pyt 机制 | 多 commit | _run_fly/run_subcase/run_pyt/expect_pass/双发现/thread_local + 140 单进程 + 8 复合 wrapper 全转 .pyt |
| decay_after_backup 接线 | fd24481 | backup 触发后衰减（事件驱动）|
| auto_backup 双层重设计 | 🔄 step 1 已做 | worker suggest + master EWMA 聚合（详见 .work/auto_backup_handoff.md）|

---

## 建议优先级

1. **文档同步**（ISSUES/roadmap/architecture 状态更正，纯文档零风险）
2. **auto_backup 双层重设计完成**（.work/auto_backup_handoff.md，step 2-5）
4. **死代码清理**（IOThreadPool/_MIGRATED_TO/GMRES，纯收益降维护噪音）
5. **网络层**（HandlerThreadPool 接线 / send 异步化 / 背压——影响性能上限）
