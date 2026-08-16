# Fly 测试审计与覆盖率报告（2026-08-16）

> 测量方式：`tools/measure_coverage.sh all`（Python coverage.py 经 sitecustomize 全进程自启 + C++ gcov/lcov，全量单测 + 全量 QA）
> 基线 commit：169d37d（文档重组批次后）
> 测试规模：单测 56 target（约 1050 gtest + 111 py 用例）；QA 162 case（157 .pyt + 5 裸 .py，其中 12 个复合 .pyt 展开 28 子 case，全量约 173 个 fly 子进程）

---

## 一、代码覆盖率实测

### Python（Master + Workers 合并，全量 QA 驱动）

**TOTAL 79%**（2625 stmts / miss 470 / branch 786）

| 文件 | 覆盖率 | 说明 |
|------|--------|------|
| mapreduce.py | 96% | |
| read_cache.py | 96% | |
| executor.py | 85% | |
| userdoc.py | 82% | |
| project.py | 90% | |
| runtime.py | 88% | |
| task.py | 90% | |
| database.py | 81% | |
| agent.py | 79% | 缺口集中在 PeerChannelGroup 边缘分支（1186-1216） |
| db_chain.py | 72% | 损坏恢复分支部分未达 |
| chain_registry.py | 69% | |
| bootstrap.py | 71% | |
| fly/__init__.py | 60% | 420-452（MapReduce 高级入口）、392-402 未达 |
| **main.py** | **48%** | 最大缺口：worker 模式入口（53-85）、交互模式（188-241）、异常路径——worker 进程在 QA 中经 main.py 启动的路径未被 coverage 采集到（子进程 coverage 依赖 env 继承，部分 spawn 路径未注入）|
| sitecustomize.py | 0%（18 行） | 覆盖率引导模块自身，预期 |

### C++（全量单测 + 全量 QA 驱动）

**TOTAL lines 85.8%（8773/10224）/ functions 78.4%（2328/2971）**（vs 2026-07-02 基线 74.1%，+11.7pp）

| 模块 | 行覆盖 | 说明 |
|------|--------|------|
| common | 97.9% | |
| serialization | 96.5% | |
| task | 96.0% | 调度器/依赖图/优先级/locality 覆盖充分 |
| log | 90.2% | |
| core | 90.0% | |
| storage | 88.6% | |
| agent | 83.4% | 最大模块（4353 行）；缺口与审计发现的 PeerRpcServer/EWMA 判定/probe 线程一致 |
| network | 81.3% | |
| main | 67.0% | 入口分支/CLI 错误路径 |

> 测量口径备注：①master_agent_test 初次被脚本 `timeout 60` 截断（90 用例需 ~118s），已用 `timeout 300 + GCOV_PREFIX` 补采后重新 lcov（脚本已修复：timeout 300 + stale target 过滤）；②QA 串行 -j1 下 162/162 全过；③branches 无数据（gcov-12 + lcov 2.x 的已知组合限制）。

---

## 二、单测审计（56 target）

### 覆盖缺口（按风险排序）

| # | 功能点 | 现状 |
|---|--------|------|
| 1 | **PeerRpcServer + PeerChannelGroup**（peer_rpc_server.cpp 344 行） | 零单测。仅 message_protocol_test 校验 PEER_RPC_* 帧字节；运行验证全靠 qa/solver 的 v2 daemon QA |
| 2 | **master 端 auto_backup EWMA 判定**（on_worker_backup_suggest → evaluate_and_maybe_backup：EWMA 衰减、双分数 OR、副本上限、大文件豁免） | 无单测且无 for_testing hook。已有覆盖仅在周边：worker 侧 suggest 阈值（data_service_test ×3）、目标选择（SelectBackupWorker ×6）、消息编解码 |
| 3 | WorkerAgent::bandwidth_probe_loop（主动探测发送线程） | 无测试（响应端/评分端有） |
| 4 | WorkerAgent::on_task_assign 依赖位置预取回填 | 无测试 |
| 5 | Python wait_obj / @as_task(requires=…) 解析 | wait_obj 仅 QA 覆盖；requires 解析的 18 例测试**游离脱管**（见死测试） |
| 6 | DataServer stop 持锁 notify 纪律（8419526 修复项） | 无并发回归测试 |

### 冗余（可零损失合并）

| # | 对 | 说明 |
|---|----|------|
| R1 | serialization/storage 两份 compressing_streambuf_test | serialization 版（6 例）是 storage 版（13 例）的早期子集 |
| R2 | write_registration_test（5 例）vs data_service_test | 三个回调用例逐条重复；仅 2 例独有 |
| R3 | master_agent_test 文件内两代 stop 测试 | DoubleStopNoCrash/StopBeforeStartNoCrash 被 L2052/L2067 新版取代未删 |
| R4 | metadata_client_test 与 message_protocol_test 的 DataLocation/DataQuery round-trip | 双份 |
| R5 | data_service_test 的 WBQ 基础用例 vs write_back_queue_test | 跨属主重复 |

### 死测试 / 游离测试

- **8 个 Python 测试文件不在任何 BUILD**（bazel test 永不执行）：agent×4（integration/dependency_scheduling/sum_example/worker_property）、log、storage/read_cache、task×2（requires_parsing/task_integration）。其中 **test_requires_parsing.py（18 例）是 @as_task(requires/vars/priority) 解析的唯一测试**——脱管意味着该特性无 CI 覆盖；test_read_cache.py 对应的 read_cache.py 功能活跃。处置：注册进 BUILD 或删除（test_sum_example.py 是 demo 脚本、与 C++ 逐条重复的 4 个可删）。
- 无编译级死测试（io_thread_pool_test 已随类删除）。

---

## 三、QA 审计（162 case）

### 覆盖缺口（按风险排序）

| # | 特性 | 现状 |
|---|------|------|
| 1 | **auto_backup 双层机制**（suggest + EWMA，architecture.md §5.4 整节） | 零 QA——全 qa/ 无 auto_backup_enabled/worker_suggest_*/master_ewma_* 引用；现有 backup QA 全停留在旧手动 backup_threshold 路径 |
| 2 | **Worker 断连宽限重连**（worker_reconnect_timeout>0 存活语义）与**重复注册 probe 拒绝** | 零 QA——现有 4 处 reconnect_timeout 全部 =0 只测"即死"；probe 路径仅 agent_network_test 单测覆盖 |
| 3 | **压缩特性**（compression_threshold 4KB passthrough/压缩开关） | 无专项断言（仅顺带经过 lz4） |
| 4 | TIER3 master 兜底回查、ObjectCache low tier 针对性断言、_MIGRATED_TO 旧库兼容回退 | 被动/零覆盖 |
| 5 | TaskSubmitAck 断连窗口提交、A/B 类消息重放（19d9afb） | 仅单测覆盖（断连场景 e2e 需要专门模拟，QA 无现成抓手） |

### 冗余（预计可减 ~10 case 零覆盖损失）

| # | 对 | 证据 |
|---|----|------|
| R1 | **qa/backup 三胞胎**：test_backup_data.pyt ≡ test_backup_load_db_multi_worker.pyt ≡ test_pending_task_persist.pyt | 三个 sub case 脚本 md5 全同（a303cd05，内容是 pending_task_persist 的 Run1）——三个 case 跑同一脚本且**名字全部名不副实**（没有一个测 backup/多 worker）；真正意图的两进程脚本（*_run1/run2.py 共 4 个）成孤儿无 .pyt 引用 |
| R2 | performance/test_freeze_write_reject.pyt ≡ storage/test_load_frozen_db_write.pyt | 字节级全同（43 行） |
| R3 | test_solver_ras.pyt ≡ test_solver_ras_n4_sd2_ov1.pyt | 参数全同 |
| R4 | mapreduce add/wordcount ⊂ coverage ≈ coverage_report | 断言子集链 |
| R5 | freeze 拒绝家族 ×5（跨 3 category） | 同一功能面 5 个 case；其中 frozen_db_write（worker 可写）与 freeze_write_reject（广播后拒写）断言方向相反，宜合并显式区分两阶段语义 |
| R6 | merge_then_solve ⊂ cross_path 版；merge_db ⊂ merge_db_cross_path | 自述子集 |
| R7 | fail_unscheduleable / no_matching_worker / unresolvable_dependency | 同场景不同 config 的三分支，可参数化合一 |
| R8 | stress ⊂ stress_stability；userdoc ⊂ userdoc_e2e | 子集 |
| R9 | solver 14 个 test_solver_ras_n* 参数矩阵文件 | 逐文件复制（仅 3 行参数不同），宜收敛为 1 个 .pyt 循环 |

### 结构问题

- backup category 命名与内容严重脱节（3/6 case 名不副实）——修复前不应在该 category 补 auto_backup 新 case。
- qa/unit/ 2 个 pytest 不以 test_ 前缀命名不被 runqa 发现（在 162 之外）。

---

## 四、近期新特性覆盖核对（2026-08-15/16 增量）

| 特性 | 单测 | QA | 结论 |
|------|------|-----|------|
| H1 backup 偏好 storage_only + 水位 | SelectBackupWorker ×6 | test_backup_prefers_storage | ✅ |
| H2 副本遍历排序/死 holder 排尾 | master_agent_test ×3 | （经 takeover/H3 case 间接） | ✅ |
| H3 判死后同 host 接管 | ×5 + hook | takeover_after_death + 阴性 + 版本选优 | ✅ |
| H4 自动补齐 + 判死提醒 + probe 防护 | spawn×2 + duplicate | test_auto_storage_spawn；probe 无 QA（单测有） | 🟡 probe 无 e2e |
| G2/G3 断连宽限/指数退避重连/上报缓冲 | ReconnectWithinGrace ×n | 零 QA（全部 reconnect_timeout=0） | ❌ QA 缺口 |
| 注册时序收口（Ack 先于调度） | WriteRegisterPendingBlocks 两段式 | —（单测充分） | ✅ |
| 断连消息 A/B 类重放 + TaskSubmitAck | 重写 + 新增若干 | 零 QA | 🟡 单测 only |
| auto_backup suggest+EWMA | worker 侧阈值 ×3；**master 判定零测试** | 零 QA | ❌ 最大缺口 |

---

## 五、结论与建议（优先级）

1. **补 auto_backup EWMA 判定的 for_testing hook + 单测 + QA e2e**（当前唯一"双侧零覆盖"的活跃机制；补 QA 前先修 backup category 结构）。
2. **断连宽限重连 QA e2e**（复用 simulate_master_disconnect 钩子——目前仅 diag 脚本使用；或加 reconnect_timeout>0 的双阶段 case）。
3. **处置 8 个游离 Python 测试**：test_requires_parsing.py 注册进 BUILD（恢复 requires 解析 CI 覆盖）、test_read_cache.py 注册或并入、其余 6 个删除。
4. **清理冗余**：backup 三胞胎归一 + 孤儿 run1/run2 接回 .pyt（恢复两进程场景）；freeze 家族合并；solver 参数矩阵收敛；R2/R3 直接删。
5. PeerRpcServer 单测（listen/connect/重连/BYE 帧——v2 daemon 的通信底座只有 QA 兜底）。
6. main.py 覆盖率缺口（48%）部分为测量盲区（spawn 路径未注入 coverage env），可在 measure_coverage.sh 对 spawn 路径补 env 透传。

> 覆盖率快照报告按文档约定不长期保留，结论沉淀于本文件与 coverage-testing.md；下次测量直接重跑 tools/measure_coverage.sh。
