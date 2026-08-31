# Fly 项目交接文档（2026-08-31）

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
