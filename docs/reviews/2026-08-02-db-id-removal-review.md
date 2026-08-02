# Code Review — db_id 去除 / base_path→db_path 改名

- **审查范围**：5 个 commit（`f7504b4..0718989`），共 78 文件 / -1986 +1883 行
  - `f15a1f8` refactor: 全量 rename db_id→db_path
  - `01eb9d0` refactor: 删除冗余 db_path 变量 + 修复 Python split + 清除 db_id 残留
  - `632b7e2` chore: 清除注释中最后的 db_id 提及
  - `3f6c7ef` refactor: 彻底删除 db_path_ 锚点 + base_path→db_path rename
  - `0718989` fix: test_locality_perf 跨轮脏数据 flaky
- **HEAD sha**：`071898905e87283a3532a073beece92a00980273`
- **方法**：5 个并行 Sonnet 审查 agent（CLAUDE.md 合规 / shallow bug / git 历史 / 历史 commit+ADR / 代码注释）+ 6 个并行 Haiku 打分 agent（0-100 置信度）
- **仓库**：`heixxxx/fly`（GitHub blob 链接前缀 `https://github.com/heixxxx/fly/blob/071898905e87283a3532a073beece92a00980273/`）

## 总体结论

机械改名（`db_id`→`db_path`、`base_path`→`db_path`）整体正确，**CLAUDE.md 合规检查全部通过**（CM* 类型、FLY_* 宏、export `ex_*` 命名、qa 不用 `sys.path.insert`、flaky 走根因修复均合规）。`01eb9d0` 修复了一处真实跨语言 bug（`_split_full_name` 固定偏移 10 → `rfind(':')`）。

按打分阈值（≥80）严格过滤，**无必须 block 的 issue**。最值得关注的一项（cross-path merge 的 idx 扫描错目录）打 **78** 分，紧贴阈值下方 —— scoring agent 完整追踪 read 路径后确认"3 个 read tier 全部失败"，**建议人工确认**。

## 候选 issue 打分汇总

| # | issue | score | 过阈值? | 处置 |
|---|-------|-------|---------|------|
| 1 | DbMeta 磁盘格式变更（跨版本 `load_db` 失败） | 45 | 否 | 详见下，技术真实但触发概率低 |
| 2 | **cross-path merge 读路径失效**（idx 扫错目录） | **78** | 否（紧贴） | **建议人工确认** |
| 3 | `register_database` 去重 guard 删除 | 22 | 否 | 非 bug（结构性不可能） |
| 4a | `set_paths` 删锚点（逻辑层） | 15 | 否 | 非 bug（设计一致） |
| 4b | ADR 0002 文档过时未更新 | 25 | 否 | 文档卫生，CLAUDE.md 未要求 |
| 5 | `test_merge_db_cross_path.py` 验证削弱 | 40 | 否 | 详见下，与 #2 叠加 |
| 6 | find-replace 残留误导性注释 | 40 | 否 | 详见下，建议顺手修 |

## 重点 issue 详述（按重要性排序，不分是否过阈值）

### 【重点关注 · score 78】cross-path merge 读路径失效

**现象**：merge worker 把新 idx / `.dat` 写到 **target** 目录，但非豁免 worker 在 cleanup 时扫描的是**源**目录，cross-path 下读到旧 idx（指向已删源 `.dat`），read 全部失败。

**证据链**（均在 HEAD `0718989`）：
- merge worker 把新 idx 写到 target：[`src/agent/cpp/worker_agent.cpp:1388`](https://github.com/heixxxx/fly/blob/071898905e87283a3532a073beece92a00980273/src/agent/cpp/worker_agent.cpp#L1386-L1411) `get_or_create_merge_writer(target_db_path, ...)` + `:1389` `register_database(target_db_path, ...)` + `:1411` 上报 `target_full`
- `MergeCleanupMessage.db_path_` 携带**源** path：[`src/agent/cpp/master_agent.cpp:2371`](https://github.com/heixxxx/fly/blob/071898905e87283a3532a073beece92a00980273/src/agent/cpp/master_agent.cpp#L2369-L2373) `cleanup_msg.db_path_ = db_path;  // 源 db_path`
- 非豁免 worker 扫源目录的 `.idx`：[`src/agent/cpp/worker_agent.cpp:1496`](https://github.com/heixxxx/fly/blob/071898905e87283a3532a073beece92a00980273/src/agent/cpp/worker_agent.cpp#L1494-L1501) `fs::directory_iterator(msg.db_path_)`

**为何同 path 正确、cross-path 出错**：默认 `merge_db_path == db_path`（[`agent.py:439`](https://github.com/heixxxx/fly/blob/071898905e87283a3532a073beece92a00980273/src/agent/py/agent.py#L438-L440)），target==source，新 idx 就在源目录，被 `test_merge_then_solve` 覆盖且通过。但 cross-path（`merge_db_path != path`）下：源目录只剩旧 idx → TIER1 读源 `data_{源writer}.dat`（已删）失败；TIER2 worker `remote_idx_[源]` 已清且未重建；TIER3 master 只重建了 target 命名空间的 `remote_idx_`（因 `:1411` 上报 `target_full`），master `remote_idx_[源]` 为空。**3 个 read tier 全失败**。

**打 78 而非更高**：scoring agent 完整验证了 read 路径，bug 真实存在；扣分仅因 cross-path 是"支持但较少用"的场景，且当前无任何测试覆盖该路径（见下条）。

---

### 【score 40 · 建议顺手修】`test_merge_db_cross_path.py` 验证被削弱

**现象**：commit `2cd9944` 加的 `assert merged_db.read_object("data/alpha") == 100` / `data/beta == 200`（Phase 3a）被本次删除。现在测试只断言 `_MIGRATED_TO` 文件存在且非空：[`qa/storage/test_merge_db_cross_path.py:70-71`](https://github.com/heixxxx/fly/blob/071898905e87283a3532a073beece92a00980273/qa/storage/test_merge_db_cross_path.py#L69-L72)

**问题**：测试 docstring（[:3-9](https://github.com/heixxxx/fly/blob/071898905e87283a3532a073beece92a00980273/qa/storage/test_merge_db_cross_path.py#L3-L9)）和 ADR 0002 §验证仍声称"用源 path 句柄读数据 —— 应自动重定向到 merge 产物"，但代码已不读。`test_merge_then_solve` 用的是默认同 path merge（[:70](https://github.com/heixxxx/fly/blob/071898905e87283a3532a073beece92a00980273/qa/storage/test_merge_then_solve.py#L70)），不覆盖 cross-path。

**与上条叠加**：本该能抓到 cross-path 回归的测试，其 read 断言被删 —— 这正是 #2 那个 bug 没被 CI 拦下的原因。建议重新加回 read-after-merge 断言（顺带就能验证 #2）。

**打 40**：本质是测试覆盖/doc 维护问题，CLAUDE.md 未明确要求"测试须验证 docstring 所述"，但 docstring 与实际不符是 actively misleading。

---

### 【score 45】DbMeta 磁盘格式变更（跨版本 load_db 风险）

**现象**：[`src/storage/cpp/db_meta.h:17-20`](https://github.com/heixxxx/fly/blob/071898905e87283a3532a073beece92a00980273/src/storage/cpp/db_meta.h#L17-L20) `DbMetaHeader` 删了 `db_id_` 字段，从 `FLY_SERIALIZE(db_id_, created_at_)` 变为 `FLY_SERIALIZE(created_at_)`。`FLY_SERIALIZE` 是 positional binary（非自描述），version tag 仍为 1。旧二进制写的 `_DB_META`（`[string db_id][int64 created_at]`）被新 reader 当成 `[int64 created_at]` 读 → `database.cpp:~525` 的 `FLY_DECODE`（无 try/catch）抛 `std::runtime_error`，影响跨版本 `load_db`/`merge_db`。

**打 45**：CLAUDE.md 未要求磁盘前向兼容；项目早期、qa 每次 run 都 `rmtree` 重建 `_DB_META`、ADR 0002 本就计划这次清理 —— 实际触发概率低。但技术上是真实的格式不兼容，且 `FLY_DECODE` 未 try/catch（对比同文件 WorkerInfo 解码 [:538-543](https://github.com/heixxxx/fly/blob/071898905e87283a3532a073beece92a00980273/src/storage/cpp/database.cpp#L537-L543) 有 try/catch）。若磁盘上可能存在旧版本 DB，建议要么加 version+条件读，要么 `load_meta_from_path` 包 try/catch 降级。

---

### 【score 40 · 建议顺手修】find-replace 残留的误导性注释

最危险的一条（可能诱导未来重蹈 `01eb9d0` 刚修的覆辙）：[`src/network/cpp/message_types.h:208`](https://github.com/heixxxx/fly/blob/071898905e87283a3532a073beece92a00980273/src/network/cpp/message_types.h#L207-L209)
```cpp
CMString object_name_;   // full name: "db_path:short_name"（db_path 为固定 10 字符前缀）
```
"固定 10 字符前缀"自 `ade4792` 起已**失真**（db_path 变长、split 改 `rfind(':')`），兄弟测试 `data_service_test.cpp:466` 反而写对了（"db_path 变长（不再是固定 10 字符）"）。

其余纯 cosmetic（同义反复/双重打印/死赋值）：
- `executor.py:59` `db_path = db_path  # db_path == db_path`（死自赋值，原 `base_path = parts[2]`）
- `data_service.h:20` / `data_service.cpp:617` 注释 "db_path 废弃：现在 db_path == db_path（db_path）"（同义反复）
- `worker_agent.cpp:1116-1117` / `database.cpp:629` DBG/INFO 重复打印同字段（原 `db_id={}, base_path={}`）
- `master_agent.h:91` "自动生成 id"（已无 id 生成）
- `storage_export.cpp:438-439` 参数标 `/*db_path*/`（实为 `existing_db_path`）

## 已验证为非 bug（明确排除）

- **`register_database` 去重 guard 删除（score 22）**：[`data_service.cpp`](https://github.com/heixxxx/fly/blob/071898905e87283a3532a073beece92a00980273/src/storage/cpp/data_service.cpp) 现 `db_paths_[db_path] = {db_path, data_path, writer_id}` 无条件 upsert。旧 guard 防的是"同 base_path 不同 db_id"——db_id 废弃后 `db_path` 已是唯一 key，该场景**结构性不可能**；上游 `MasterAgent::register_database` / `db_instances_` 也已 dedup。理论顾虑，无实际触发路径。
- **`set_paths` 删锚点（score 15，逻辑层）**：[`database.cpp`](https://github.com/heixxxx/fly/blob/071898905e87283a3532a073beece92a00980273/src/storage/cpp/database.cpp) master 所有句柄共享同一 C++ `Database` 对象（`get_database` 返回 `db_instances_[源path]`），`set_paths` 正确 `unregister(旧) → db_path_=新 → register(新)`，full_name 统一翻转到 target；`db_instances_` **刻意保留源 path 作路由 key**（[`master_agent.cpp:2446-2458`](https://github.com/heixxxx/fly/blob/071898905e87283a3532a073beece92a00980273/src/agent/cpp/master_agent.cpp#L2446-L2458)），DependencyGraph 在 cleanup 时用 target 前缀名重建，无悬挂边。设计内部自洽。`wait_for_all_tasks` 保证 merge 时无 pending 边。
- **merge 源/目标读写分离**：[`worker_agent.cpp:1358-1416`](https://github.com/heixxxx/fly/blob/071898905e87283a3532a073beece92a00980273/src/agent/cpp/worker_agent.cpp#L1357-L1416) 读用 `source_full`、写/上报用 `target_full`，正确。
- **`MergeCleanupMessage.db_path_` 用源 path**：与 `pending_merge_cleanups_` key（一直就是源 path）匹配，ack 路径一致。
- **`__fly_db__` 3 字段格式**：`task.py` 序列化 / `executor.py` `split(":", 2)` 反序列化一致；db_path 不含 `:`（构造时拒绝），3 段 split 安全。
- **Python `_split_full_name` 改 `rfind(':')`**：与 C++ `data_service.h` 一致，修复了 `ade4792` 引入的"固定偏移 10 对变长 db_path 失效"隐患。
- **`0718989` flaky 修复**：根因修复（每轮独立 db_path 隔离，避免 `DataService` 的 `local_idx_/remote_idx_` 跨轮残留导致 holders 错位），非 timeout/retry 掩盖，符合 CLAUDE.md "flaky 零容忍 = 修根因"。

## CLAUDE.md 合规

逐项核对（唯一相关 CLAUDE.md 是 `/root/fly/CLAUDE.md`）：
- 命名规范（CM* 类型、module-style include、`ex_*` export 命名）—— **通过**
- FLY_* 宏（`FLY_SERIALIZE` / `FLY_EXPORT_*`，无裸 bitsery/nanobind）—— **通过**
- flaky 零容忍（`0718989` 走根因）—— **通过**
- qa 不用 `sys.path.insert`、用 `get_fly_binary()` —— **通过**
- 测试稳定性（无新增 `sleep()+assert()`）—— **通过**
- qa 清理安全（无 `rm -rf test_*` glob）—— **通过**

---

*Generated by ZCode local code review（5×Sonnet 审查 + 6×Haiku 打分）。仅落盘本文件，未做任何 git add/commit/push、未评论。*
