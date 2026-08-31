# Fly 项目交接文档（2026-08-31 · 第三次更新：全部收口）

> 首轮交接见文末历史节。本轮已完成：sd9/project 破案 + 执行上提重构 + T2b~T7 全部落地。
> **T7 已收口：-j6 ×100 轮稳定性全绿（2026-08-31，100/100）。无未决阻塞项。**

## 〇-EXIT、T7 收口 + 竞态修复 + 工具链并发安全（当日后半程）

1. **§〇-A race 修复（280fc10）**：按用户裁定实施——`PeerRpcWireStatus::NOT_READY=4`
   协议一等错误码（弃字符串约定，payload 只带诊断消息）+ check 圈级收集
   （NOT_READY 跳过请求下一成员、全员集齐才开始计算、圈间固定退避 10ms、
   累计 30s 兜底判死；断连/真失败/poison 判死语义不变）。两命中场景 ×10
   绿 + 全量 QA 绿。
2. **runqa 并发安全（4742e5f）**：超时善后弃全局 pgrep/pkill（曾把并发轮
   14 个无辜 case 连环误杀——`-j6` 首轮 14 连败的真相，孤儿 runqa 与新轮
   互踩所致），改 `_group_pids(pgrp)` 进程组精确打击；stability trap/轮超时
   补 runqa 进程树清理。**期间教训存档**：一次错误立项（"-j6 暴露 7 个时序
   敏感缺陷"）与一次错误归因（"CPU 超订假失败"）均被用户否定——最终结论：
   误杀连坐是工具缺陷，2 物理核跑 -j6 并不拥挤；排查中另一关键转折是发现
   两份日志失败集错开 = 双 runqa 互写（孤儿实锤的突破口）。
3. **runqa 时间可观测性（405ea8e）**：套件横幅/Summary 带时间戳，每 case
   打印 ▶ START 行与 [start → end] 区间（并发排布与长尾定位）。
4. **stability 轮间缓冲 20s→5s（用户裁定）**。
5. **T7 验收**：`-j6` ×100 轮（分段 32+68，中段隔 runqa 时间戳日志与 5s
   缓冲两处惰性变更），**100/100 全绿**（167 case × 100，每轮 84-113s）。
   产物：`.work/stability/20260831_161953`（1-32）+ `20260831_172209`（1-68）。
6. 仓库根 36 个 `fly_log.N` 历史遗留（裸跑烟测，AGENTS.md 禁令入册前累积）
   清扫完毕；机制加固经用户裁定不做。

## 〇、本轮完成摘要（每步均验证：C++ 单测 73/73 + 全量 QA 167/167）

1. **sd9/project "死锁" 破案**：非 PeerRpc 缺陷。根因链：
   - `poll_task_blocking` 经 nanobind 裸绑定（无 GIL release），worker Python 主循环持 GIL 阻塞在 cv wait 100ms
   - → 同进程 Python 线程（solver serve）唤醒被压制**精确 100ms**（实测 ENQUEUE→SVC 恰好 100.0ms）
   - → 每 PeerRpc 往返 +100ms；sd9 需 110 轮 × 9 成员 × 100ms ≈ 99s，60s case timeout 必挂
   - master "死亡" 实为 test 框架超时 teardown 顺杀，非崩溃
2. **执行上提重构**（用户架构裁定：除初始化与 main 入口外禁止 C++→Python 反调）：
   - C++ 原语 `take_task`（GIL 释放等待+出队+begin 钩子；internal task 就地消化）/ `finish_task`（end/report/上报链）
   - 导出层 take/finish（GIL 释放）；删 `poll_task*`/`set_executor`/`set_exec_func` 绑定
   - Python：`Worker.poll_loop`（main.py 主循环）；`set_exec_func` 反调点消灭
   - **性能：RPC 单跳 100ms → 0.3ms（333×）；project case 62s 超时 → 2.7s；sd9 62s → 3.6s**
3. **flows（SolverProject）迁移缺陷修复**：kickoff 非阻塞化（修 AttributeError/自等死锁）、`__rasg__sol` 链尾持久化产出、check 宿主编队、case worker 池 nsd+1
4. **T2b（d423258）死 API 清理**：`_write_pickle_bytes`×3/`_read_streaming`×2/`_read_decompressed`×2/`_is_temp`/`_decompress_bytes`/`_compress_pickle_bytes` 导出删 + 4 个 QA 测试迁移恒流式；storage_test 补 `_drain()`（裸 pytest 进程无 task 收尾排空）
5. **T2c（fd7a97a）写侧恒流式**：删 `streaming_write_threshold` + 非流式分支 + `_commit_stream`；用户裁定落地：调用仅存在于测试的 API 即过期——C++ `commit_stream`/`write_pickle_bytes`/`compress_pickle_bytes` 删，测试造数 helper 全量迁 `open_write_stream → write → finish_and_commit`；本地 entry hash 有意留空（master register 权威），BareWriteObject 测试按新语义更新
6. **T2d（90d2540）temp 写流式化**：`open_write_stream(name, py_name, temp=true)` 参数化 sink——pickle.dump 直入 temp_writer_ 增量直写（R+常数），删 `write_temp_pickle` + `_write_temp_pickle` 导出
7. **T3（77ee15b）coarse 双对象拆分**：`__rasg__coarse_prebuilt` → `coarse_static`（只读，默认 low）+ `coarse_ac`（splu 原地重排可变，消费 cache="none"）——污染防护由消费拷贝简化为分层隔离
8. **T4（59f94fa）ObjectCache 单层化**：low_ 池全集删除（零生产消费）；保留 high（typed 对象快路径）；`ex_stg_cache_stats` 改 4 元组；eviction/保护窗/计分测试迁 high 层保留覆盖
9. **T5（cc17523）DIGEST 根摘要双侧消除**：serve 分片流取消 root.update 单遍累积（发 0）；client 双侧（network_chunk_source/data_client_pool）`root_crc≠0` 才验（兼容旧 serve）；DIGEST 帧保留作流结束标记
10. **数值结论（已证，单进程模拟）**：sd9(n50/r30/o1) 纯 RAS 固有 ~110 轮收敛（v1/dynamic 数学逐位等价）；n20/sd4 需 48 轮。"≤20 轮"量级属 coarse 模式。修 100ms 后时长不再是约束。
11. T2a 遗漏单测迁移：test_ras_graph_io.py `from ras_graph import` → `ras_graph_dynamic`。

## 〇-A、【已修复关闭】ras_matrix 偶发 "no ctx" race（修复 280fc10，T7 100/100 验收）

> **T7 稳定性测试结论（2026-08-31 13:33）**：100 轮跑到第 19 轮即命中本 race
>（`test_ras_graph_dynamic.pyt`/rasgd_early_stop：`[RASG DYN CHECK] t=0 rpc
> sd=0 status=2 at step=0`）——与 ras_matrix 同根因（check 首请求早于
> compute 注入，无依赖边），在稳定性负载下发生率 ~1/19 轮。**该 race 修复前
> 100 轮稳定性无法通过，T7 阻塞于此。** 第二现场（含全链日志）保留于：
> `.work/stability/20260831_125154/failure_round_19/`（前 18 轮全绿产物在同
> 目录 round_001-018.log）。

**现象**：全量 QA 第三轮中 `test_solver_ras_matrix.pyt`（solver_ras_param，n6/sd3/ov1）失败 1 次；同二进制复跑 6/6 绿。两轮全量 + 一轮失败 = 偶发。

**已抓证据**（失败轮 master.log + w4 日志；**原日志已被复跑覆盖**，以下为摘录）：
```
11:05:24.657 [RASG DYN SETUP CHECK] gen=b4f65c05 pool of 3 connected
11:05:24.661-663 compute_dyn×3(100005-7) + check_dyn(100008) 提交
11:05:24.665 Executing task: 100008 (check_dyn, w4)
11:05:24.677 Task complete: 100007 (w3 的 compute_dyn —— 注入完成于 check 开跑后 12ms！)
11:05:24.686 PeerRpcServer stopped ×2（stop_peer_rpc 的正常一对：显式 stop + unique_ptr 析构）
11:05:24.687 Task execution failed: [RASG DYN CHECK] t=0 rpc sd=2 status=2 at step=0
```

**根因已定位（机制清楚，修复方案已设计未实施）**：
- `status=2` = `PeerRpcStatus.ERROR`（对端 `respond_failure`），**不是连接失败**
- `_serve_loop` 收到请求时 `shared["step_ctx"] is None` → 回 "no ctx" failure → check 判组死 raise
- **调度窗口**：compute_dyn（注入 step_ctx，w1-w3）与 check_dyn（w4）跨 worker 并行；check 的 inputs 只依赖 `b_0`，**没有等 compute 注入完成的依赖边**。check 首请求可比 w3 的注入早到（失败轮早 12ms）
- v1 不炸：其 check(step N) → compute(step N+1) 链式 inputs 依赖天然消除此窗口。dynamic 拆分"注入/驱动"时引入

**修复方案（已实施 280fc10：NOT_READY 错误码 + 圈级收集重试，见 §〇-EXIT.1；原静态依赖边方案经 review 被否，改就绪语义 + 调用方韧性）**：
1. compute_dyn 尾部写就绪对象 `db.write_object(f"__rasg__d_ctx_{gen}_{sd}_{t}", True)`（持久化，勿用 temp——master 调度依赖查询对 temp 的可见性未验证）
2. check_dyn 的 inputs lambda 加 `[db.get_full_name(f"__rasg__d_ctx_{gen}_{s}_{t}") for s in range(nsd)]`
3. teardown/cleanup 补删该对象族（防重投残留撞 provenance）
4. 验证：ras_matrix ×10 轮 + 全量 QA

**附带观察**：复现概率低（~1/7），压测可提密度：`for i in $(seq 20); do ./qa/runqa qa/solver/test_solver_ras_matrix.pyt || break; done`。

## 〇-B、调试基建（本轮新增，保留）

- `ConnectionManager::get_peer_info(conn_id)` / `TcpConnectionManager::peer_info_by_fd(fd)`：连接对端指纹（fd+addr:port），连接漂移诊断用（connection_manager.h / tcp_connection_manager.cpp）

---

# 以下为首轮交接原文（历史参考；任务状态以上方 §〇 为准——T2a~T5 均已完成）


> 上一个会话完成了 V2 chunked-transfer 性能优化、恒流式改造、缓存双池落地、求解器收敛（进行中）。
> 当前阻塞在 T2a 的 sd9/project PeerRpc 死锁（已有详细 debug 结论与验证计划）。

## 一、总任务表（用户已批准的完整计划）

| # | 任务 | 状态 |
|---|------|------|
| T1 | 缓存主体收尾（默认 low + is_temp META 链 + 写预热 + 哨兵） | ✅ 已 commit（工作区） |
| T2a | 求解器收敛：退役 ras.py/v1/v2，仅留 dynamic | 主体完成，**sd9/project 死锁阻塞** |
| T2b | 死 API 清理（6 个）：_read_streaming/_read_decompressed/_write_pickle_bytes/_decompress_bytes/_is_temp/_write_temp_pickle + 测试迁移 | 待做 |
| T2c | 写侧恒流式：删 streaming_write_threshold + 非流式分支 + _commit_stream export | 待做 |
| T2d | temp 写流式化：open_write_stream 参数化 sink（temp→temp_writer 增量直写） | 待做 |
| T3 | dynamic coarse 拆分：coarse_static（默认 low）/ coarse_ac（显式 none） | 待做 |
| T4 | C++ ObjectCache 对齐：删死 low_ 池/level 化/默认 populate low | 待做 |
| T5 | DIGEST wire 根摘要双侧消除：serve root.update 发 0 + client root_crc≠0 才验 | 待做 |
| T6 | 文档汇总 + 统一 push（**每需求 push 前必须更新文档——用户明确裁定**） | 待做 |
| T7 | 100 轮稳定性（受控配方：bazel shutdown 后跑，内存看门狗） | 待做 |

独立遗留：`.wslconfig` memory=5GB（用户自己操作）；跨进程缓存失效通用方案。

## 二、当前 git/工作区状态

- 已 push 到 origin/main：直到 `a410025`（恒流式改造）
- **本地未 push 的 commit**：`b2c8500`（raw 接口删除）、`1771408`（双拉修复）、T1 的缓存主体 commit、`7898c0e`（ReadCache 双池）——这些已 commit 未推送
- **工作区未 commit**：T2a 求解器收敛的全部改动（三文件删除+函数族搬家+7 case 迁移+teardown 幂等修复）
- `git stash list` 有一个历史 stash（9cbd808 的 WIP，与当前无关，勿动）

## 三、sd9/project 死锁 —— debug 结论（核心交接内容）

### 现象
`test_golden_n50_sd9.pyt`（10 worker）和 `test_solver_project.pyt` 挂起 35-60s 超时。solver 目录其余 14/17 case 已过。

### 已实锤的事实链（gdb 双端抓栈，非推测）
1. check 侧（w10）Python 主线程阻塞在 `peer_rpc_call → PendingRpcMap::wait_for` —— 证明 `send_request` 返回 true（否则立即 FAILED），**请求帧已成功 write 到某个 socket**
2. 成员侧（w1）`PeerRpcServer::server_loop` 在 `epoll_wait(timeout=10)` 空转 35 秒 —— **epoll 上既无该连接的数据事件也无 ACCEPT 事件**
3. 结论：**数据进了一个"既不是 w1 监听口、也没被 w1 注册"的 socket**（发送成功但对端从未收到）

### 完整时间线（从 master.log/worker 日志核实）
- kickoff(100001) → setup_compute×9(100002-100009, w1-w9) + setup_check(100010, w10)
- w10 的 setup_check 完成：`pool of 9 connected`（9 条 peer 连接建立），随后提交 compute_dyn×9(100011-100019) + check_dyn(100020)
- compute_dyn×9 被调度执行（w1 executing 100011），serve 常驻线程 `recv_request` 等请求
- check_dyn(100020) 在 w10 执行：对每个成员 `peer_rpc_call` → 等响应 → **双端互等死锁**
- master 侧 20 submit / 20 complete 平衡，无 failed task——不是 task 层面问题

### 三个候选机制（按嫌疑排序）
1. **conn_id 漂移**：send 按 `transport_->send(conn_id, frame)` 定向发送。check worker 同时持有 master 长连接/DataServer/9 条 peer/probe。若 pool 里某 conn_id 经历"连接关闭→fd 回收→新连接复用同号"，send 把 RPC 帧发给错误对端（如 master），对端收到不认识的 PEER_RPC_REQUEST 帧静默丢弃——与"send 成功+对端 epoll 无事件"完全吻合
2. **成员侧 accept/注册路径持久化丢失**：TCP 握手由内核 backlog 完成（connect 成功≠对端已 accept+注册 epoll）。若 server_loop 对某连接的 ACCEPT 处理有条件分支跳过注册（非时序窗口——时序窗口微秒级不可能 35s），数据永远无人消费
3. **check 侧 stop_peer_rpc 连锁**：setup_check 与 check_dyn 是同 worker 先后两个 task。若 setup 后 stop_peer_rpc 把 client 连接一并关闭，pool 里 conn_id 悬空——后续 send 走 fd 复用路径，效果同候选 1

### 为什么 rasgd 不炸而 sd9 炸
rasgd nsd=4（5 worker），sd9 是 10 worker——连接数/task 切换密度/fd 复用概率不同量级；golden 链路（solve_once 单步）时序模板与 rasgd 不同。规模敏感的既有缺陷，迁移只是把场景接进来。

### 验证计划（下一步执行）
1. 在 `send_request`（src/agent/cpp/peer_rpc_server.cpp:190）与 `TcpConnectionManager::send` 加连接对端指纹日志（conn_id → remote addr/port），`server_loop` ACCEPT 分支打印注册事件——一次复现即可判定候选 1/3（发送目标端口≠成员监听端口）还是候选 2（端口对但没注册）
2. 命中候选 1/3：修复方向 = pool 有效性校验（call 前校验 conn_id 存活）+ 发送失败/断连 fail-fast（当前 timeout<=0 是 24h 等待，掩盖故障）
3. 命中候选 2：修 server_loop 连接注册路径

### 复现方法
```bash
cd /root/fly && rm -rf .work/sd9_dbg && timeout 100 ./build/bin/fly --log-dir .work/sd9_dbg qa/solver/test_golden_n50_sd9.py &
# 等 ~45s 后 gdb attach 任一 --worker 进程
gdb -p <pid> -batch -ex "thread apply all bt 10"
```
现场证据已存 `.work/sd9_hang_evidence/`（双 gdb 栈 + master_view 全链日志）。

## 四、T2a 已完成的改动明细（工作区）

1. **删除**：`src/solver/py/ras.py`、`ras_graph.py`、`ras_graph_daemon.py` + qa/scripts/ 8 个死基准脚本 + `test_is_coarse_cache`
2. **搬家到 ras_graph_dynamic.py**：`_load_matrix`/`_get_matrix_data`/`generate_poisson_matrix`/`compute_exact_from_matrix`/`compute_exact_solution`/几何函数族（`_factor_nsd` 等 5 个）/`_compute_coarse_arrays`/`_build_coarse_operators`/`_prebuild_coarse_in_coord`/`_coord_prebuild_pipeline`（注意：v2 的 `_prebuild_coarse_grid` per-worker LU 预分发**不搬**——dynamic 的 LU 由 setup_check 自建）
3. **新增 `solve_once`**：dynamic 单步封装，返回 {"x","iters","converged"} 兼容 v1 返回结构
4. **`__init__.py` 收敛**：导出 solve_ras_graph_dynamic/get_dynamic_result/solve_once/generate_poisson_matrix/MATRIX_OBJ_KEY/compute_exact_*
5. **迁移 7 处调用方**：golden_solver、flows._solve_kickoff_task（结果写 `__rasg__sol` 保持 freeze 依赖契约）、test_ras_graph、solver_ras_param（矩阵入库替代 ras.py 内置生成）、noconv case（语义修正为小规模收敛回归）、v2 两 case、verify_2d_partition
6. **修复 dynamic 两个既有 bug**：teardown/cleanup 的 `remove_cache` KeyError（非 coarse 模式 lu/P key 未 put 时炸 controller → dynamic_done 永不产生 → wait_obj 报 cannot be produced）——改为 has_cache 守卫幂等清理
7. **v1 coarse 消费拷贝修复保留在搬家代码中**（splu 原地重排防护，T3 拆分后可简化）

## 五、关键技术结论（本会话沉淀）

### scipy splu 污染案（已根治）
scipy `splu` 对三数组零拷贝构造的 CSC 会**原地重排**传入数组（列内排序）——solver 直接引用 ReadCache 对象导致缓存条目被污染 → 动态多轮二次消费时 data/indices 配对不自洽 → 数值不收敛。修复：消费前 `np.array()` 拷贝。`FLY_CACHE_GUARD=1` 环境变量启用 ReadCache 污染哨兵（populate 快照 hash + 命中对比）。

### 缓存三分层规范（用户裁定，已写入 §14.12）
| 层 | 语义 |
|----|------|
| db 对象 + 默认 low | 只读数据跨 task 复用（调用方保证只读） |
| db 对象 + cache="none" | 会被修改的数据，每次全新反序列化 |
| agent put_cache（worker 级） | 修改后的结果跨 task 复用（如 LU 分解） |

### 重要用户裁定链（不可违反）
- **每需求 push 前必须更新文档**（§14 实施记录/python-api/性能报告——遗漏即违规）
- 常规读恒流式（无 threshold 逃生口）；仅非反序列化场景保留全量拉取
- 单对象缓存不设预算上限；命中不升级；get 不分级
- temp 压缩 record 不驻内存（恒在盘）；`.temp.` 前缀命名
- 求解器仅保留 dynamic（单次=多时间步单步）
- 文件编辑必须用 Edit 工具（禁 python/sed 批量改）
- runqa 传 .pyt 接管 case 必须传 .pyt 否则 pyt-load 假失败
- bazel build/test 后跑 runqa 前 `bazel shutdown`（防 OOM）

## 六、下一步行动序列

1. **【最优先】sd9 死锁验证计划**（上文第三节）→ 根因修复 → solver 目录 17/17 绿
2. T2a 收尾：全量 QA 167/167 + 文档（求解器收敛记录写入设计文档 §14.13 或独立小节）+ commit
3. T2b → T2c → T2d → T3 → T4 → T5 → T6（文档+统一 push）→ T7（100 轮稳定性）

## 七、建议加载的技能

- `systematic-debugging-analysis`（sd9 死锁继续排查时）
- `resolve-issues-not-ignore`（修复时勿绕根因）

## 八、关键文件索引

- 设计文档：`docs/chunked-transfer-design.md`（§14.10-14.12 为最近实施记录）
- 性能报告：`docs/performance-analysis-2026-08-30.md`
- 死锁现场：`.work/sd9_hang_evidence/`
- PeerRpc 代码：`src/agent/cpp/peer_rpc_server.cpp`（send_request:190 / server_loop:100-190）、`src/agent/cpp/worker_agent.cpp`（peer_rpc_call:2940 / recv_request:2991 / start_peer_rpc_listen:2908）
- 连接管理：`src/network/cpp/tcp_connection_manager` 相关（send 按 conn_id 定向）
