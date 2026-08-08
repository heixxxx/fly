# Fly 代码审查报告

> 审查日期: 2026-08-08
> 审查范围: 全项目（170 个 C++ 文件、273 个 Python 文件、55 个 BUILD 文件）
> 方法: 6 个并行审查 agent 分模块深挖 + 全局静态扫描 + 关键发现交叉验证

---

## 一、影响正确性的真 Bug（高优先）

| # | 位置 | 问题 | 验证 |
|---|------|------|------|
| **B1** | `fly.sh:293-294` | `check` 命令在**非函数上下文**（顶层 `case`）用了 `local` 关键字 + `return`，`set -euo pipefail` 下执行到 `local test_output` **立即报错退出**。`./fly.sh check` 实际无法运行测试。3 份文档都推荐该命令。 | ✅ 实测 `bash -c` 复现报错 | ✅ 已修复 |
| **B2** | `master_agent.cpp:1734-5,1811-2,2258-9` | **复制粘贴重复赋值**：`msg.db_path_ = db_path; msg.db_path_ = db_path;` 连续两行相同，字段重命名残留 | ✅ grep 确认 | ✅ 已修复 |
| **B3** | `worker_agent.cpp:1123` | 日志占位符重复：`"db_path={}, db_path={}"` 两个占位符都填 `msg.db_path_`，第二个应为别的字段 | ✅ 确认 | ✅ 已修复 |
| **B4** | `storage/cpp/database.h:187` | `compression_level_` 配置**写后不读**：构造时从 config 读取，但各 Compressor 用硬编码默认 level。用户调 `compression_level` 配置**静默不生效** | ✅ 全仓 grep 确认无读取点 | ✅ 已修复（TDD） |
| **B5** | `solver/ras_graph.py:225-240` | `_COARSE_CACHE` 进程级 dict 缓存**永不失效**——worker 常驻跑多次 solve（不同 omega）时，第二次会读到**上一次的错误缓存值** | ✅ 确认无清理逻辑 | ✅ 已修复（TDD） |

---

## 二、冗余死代码

### 2.1 完整的死类 / 死功能块

| 位置 | 内容 | 状态 |
|------|------|------|
| `network/cpp/io_thread_pool.{h,cpp}` + export | **`IOThreadPool` 整个类**无任何非测试消费者，与 `HandlerThreadPool` 功能重叠 | ✅ 零生产调用 |
| `solver/export/solver_export.cpp:174-246` | **8 个 GMRES 向量算子导出**（`EXSlvSparseMatrix`/`vec_norm`/`vec_dot`/`vec_scale`/`vec_axpy`/`vec_sub`/`vec_back_solve`/`vec_xpay`）全库零调用 | ✅ Python 侧 0 引用 |
| `agent/cpp/master_agent.cpp:1952-1996` | **整套 SIGTERM 优雅退出机制**（`sigterm_handler`/`check_shutdown_request`/`drain_thread_`）从未接通——main.cpp 没注册 `signal(SIGTERM,...)`，heartbeat loop 没调 `check_shutdown_request()` | ✅ 确认 |
| `storage/cpp` 的 `_MIGRATED_TO` 机制 | 注释自述"机制已废弃（db chain 取代）"，但 `MigrationHeader`/4 个方法/`migrated_db_paths_` 缓存全保留，写侧（`write_migration_marker`/`set_migrated_path`）零调用，读侧永不触发。~130 行 + 每个 new db 一次 stat | ✅ 确认 |
| `qa/scripts/` (23 个文件) | **全部 `debug_*.py`(9) + `bench_*.py`(10) + `sweep_*/profile_*`(4) 零引用**——runqa 只发现 `test_*.py`，这些一次性脚本从未被自动化调用，其中 `bench_ras3.py`/`profile_ras3_timing.py` import 不存在的 `solve_ras3`（dangling） | ✅ 确认 |
| `qa/runqa:128-165,209-265` | **`.pyt` 配置机制**（~60 行 setup/teardown callable），全仓无 `.pyt` 文件，完整死代码 | ✅ `find` 0 文件 |

### 2.2 写后不读的死字段 / 死变量

| 位置 | 内容 |
|------|------|
| `storage/cpp/database.h:195` `temp_objects_` | `mark_temp()` 往里 insert，全仓**无任何读取**（find/iterate/erase），孤儿内存持续增长 |
| `storage/cpp/database.cpp:431` `removed_objects_` | 唯一读取是 freeze 里一个"待实现"的 TODO 分支，实际只读 `.size()` 打日志 |
| `qa/runqa:412,421` `fail_fast_triggered` | 初始化+赋值，**从未被读取** |
| `qa/runqa:442` `mark` | `"✓" if r["ok"] else "✗"` 计算后从未使用（本意是打印） |
| `fly/mapreduce.py:320` `short` | 循环内计算，从未使用 |

### 2.3 仅测试调用的 public 方法（生产无消费者）

`DataService::on_object_written`、`decay_remote_access`、`get_access_read_count`、`get_db_path_for_object`、`is_write_back_running`、`pending_count`、`CompressorFactory::create_from_name`、`LocalIndex::save_legacy`、`MessageProtocol::get_payload_size`、`Reactor::connect`、`MasterAgent::restore_master_idx`/`send_idx_load_commands`/`rebuild_remote_idx`（已被 `*_for_worker` 取代但未删）、`storage` 导出 `_compress_pickle_bytes`/`_put_temp_data`。

### 2.4 传输但不用的消息字段

`RegisterAckMessage.master_address_/master_port_`（master 填充，worker 不读）、`DataRequestMessage.request_id_/requesting_worker_id_`（worker 填充，server 不读）、`TwoSegment.raw_ptr/raw_len`（设计零拷贝但消费方忽略）。

### 2.5 未使用的枚举值

`TaskStatus::CANCELLED`（无任何赋值代码）、`TaskExecStatus::TIMEOUT`（executor 永不返回）、`PendingBackup` 结构体（从未实例化）。

---

## 三、过度 trick / 脆弱设计

| # | 位置 | 问题 | 严重度 |
|---|------|------|--------|
| **T1** | `message_protocol.h:77-85` | `get_type` 把**合法高频类型 `REGISTER`(值1)** 当所有错误路径的哨兵返回。调用方无法区分"真注册消息"和"解析失败"。靠下游二次校验兜底 | 高 | ✅ 已修复（TDD） |
| **T2** | `data_server.cpp:285` + `data_client_pool.cpp:191-194` | **用 `error_message_` 魔法字符串**（`"DATA_NOT_READY"`/`"OBJECT_NOT_FOUND"`）承载协议状态码，client 用字符串字面量比较反解析。拼写/大小写/协议演进都会静默破坏分派 | 高 | ✅ 已修复（TDD） |
| **T3** | `storage/cpp/data_server.cpp:13-16` | 文件顶部 `#undef DBG` + `#define DBG(...) ((void)0)` 硬关日志 | 中 |
| **T4** | `storage/cpp/data_server.cpp:207-313` | `on_readable` 用 `std::move` 偷走 `recv_buf` 处理后再塞回，`pushed_response=true` 分支剩余半包被丢弃 | 中 |
| **T5** | `tcp_socket.cpp:132` | `sendv(const iovec*)` 用 `const_cast` 修改 const 参数推进游标 | 中 |
| **T6** | `fly/bootstrap.py` | 整套 lazy 代理机制只为服务 1 个模块（`SolverProject`），ROI 极低 | 中 |
| **T7** | `message_protocol.h decode()` | decode 顺手 `buffer.erase()` 消费字节——名为 decode 却改入参 | 中 |
| **T8** | `serialization_macros.h:15-22` | cereal 后端开关分支永不触发（全库无人定义 `FLY_SERIALIZATION_BACKEND`） | 低 |

---

## 四、严重代码重复

| # | 位置 | 问题 |
|---|------|------|
| **D1** | `qa/solver/test_solver_ras_*.py` (14 个文件) | 每文件 62 行，文件间仅 ~10 行差异，~84% 重复（520 行可压缩到 ~80 行参数化） |
| **D2** | `solver/export/solver_export.cpp` | **7 处**完全相同的 8 行 COO→SparseMatrix 样板代码 |
| **D3** | `ras_graph_daemon.py:57-169` vs `ras_graph.py:278-297` | coord 预构建逻辑重复 ~80 行 |
| **D4** | `solver/ras_graph.py` vs `ras_graph_daemon.py` | **两套 BFS 实现**（Python set 版 vs numpy 向量化版） |

---

## 五、难维护代码（超长函数 / 过深嵌套）

| 函数 | 行数 | 问题 |
|------|------|------|
| `data_service.cpp:886` `read_raw_compressed` | **155 行** | 三层 tier 挤在一个 `while(true)`，4 层嵌套 |
| `agent.py:384` `Master.merge_db` | **234 行** | 6 个 Phase 混杂 |
| `master_agent.cpp:491` `schedule_tasks` | **121 行** | 5 个职责混杂 |
| `ras_graph_daemon.py:289` `check_daemon_task` | **175 行** | 收发+数值+协议全揉一起，5 层嵌套 |
| `ras_graph.py:720` `ras_graph_compute` | **135 行** | 含不可达的 aitken 死分支 |
| `data_client_pool.cpp:25` `request` | **170 行** | 7 处重复的 close+release+return 错误处理 |
| `master_agent.cpp:2350` `cleanup_after_merge` | **126 行** | 6 步骤混合 |

---

## 六、陈旧文档与残留文件

### 6.1 文档与代码严重脱节

**`AGENTS.md` 第 45-60 行 "Public API Export Chain"** 描述了 5 步导出链（加 `__all__` / `try/except` 双布局），**实际代码**：`fly/__init__.py` 是裸 import（无 try/except），全代码库无任何 `__all__`，CLAUDE.md 第 177 行明确写「禁止 `__all__`」。

### 6.2 被 git 跟踪的残留文件

| 文件 | 问题 |
|------|------|
| `BUILD.bak` | 旧顶层 BUILD 备份，引用已移除的 `hedron_compile_commands` 依赖 |
| `callgrind.out` | 0 字节空文件 |
| `profiling_report.md` | 一次性 profiling 报告 |
| `.omo/skills/` + `.sisyphus/skills/` | 完全相同的 `fly-build` skill 副本 |
| `docs/superpowers/` (5 文件) | 一次性历史计划文档 |

### 6.3 陈旧 API / 兼容残留

- `storage` Database 构造的 `worker_id`/`existing_db_path` 两个废弃参数
- `ex_stg_create_database_with_path` 注释自述"existing_db_path 已废弃忽略"
- `message_protocol.h:108` 注释引用已删除的 `compressed_data_` 字段
- `solver/flows.py:27` `_solve_ras_graph` legacy compat 别名从未使用
- `solver` v1 (`ras.py`) 与 v2 (`ras_graph.py`) 半迁移并存
- `fly/sitecustomize.py` / `main.py` 的旧覆盖率百分比注释

---

## 七、其他

- `storage/py/database.py:427,524` 用 `from storage.py.db_chain import`，违反 AGENTS.md 规范
- `tools/measure_coverage.sh:159` 硬编码模块列表漏掉 `message`/`solver`/`test`，覆盖率静默漏报
- `qa/solver/test_solver_ras_*.py` 普遍 `f"...{const}..."` 但 `{}` 内无变量（pyflakes: 107 处）
- `master_agent.cpp` 变量名 `task_opt3`/`task_opt4`/`task_opt5`/`db_it2`（前缀 1/2 已删）
- `process_info.cpp:11-19` `hostname()` lazy init 多线程首次调用有数据竞争
- `core/cpp/system_info.cpp:114-121` `local_ip()` 注释与实现不符

---

## 总体评价

代码整体质量较高：分层清晰、宏封装扎实、并发约束注释详尽。主要问题集中在：历史重构遗留、半成品功能堆积、一次性脚本/文档未清理、协议层脆弱 trick。

**建议处理优先级**：先修 5 个正确性 Bug（§一）→ 加固协议层 trick（T1/T2）→ 清理死代码（§二）→ 文档纠偏（§六）→ 重构超长函数（§五）→ 参数化重复测试（D1）。
