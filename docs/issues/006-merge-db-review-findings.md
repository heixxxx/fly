# Issue 006: `fly.merge_db` (commit 70fd97a) 代码审查发现

## 概述

**日期**: 2026-07-28
**审查对象**: commit `70fd97a` — `feat: fly.merge_db 主动 API — 跨机数据集中到 master host`
**审查范围**: 22 文件，+1785/-199（消息层 / master+worker agent / Python 编排 / storage 辅助接口 / C++ 集成测试 + 3 个 QA 测试）
**设计文档**: [`docs/db-merge-design.md`](../db-merge-design.md)

**总体结论**: 设计扎实、实现可工作、文档优秀。关键决策（双路径契约利用、方案 B 独立 DataWriter、精确重建 remote_idx）都是踩坑后的正确选择，且全部记录了权衡。偏离"简洁优雅"的主要是 Python 编排层用 `sleep` 兜底同步，以及实现了一套从未使用的 ack 等待机制。有 1 个真正的健壮性缺陷（删源不等待 ack）。

**验证状态**:
- ✅ 编译通过（`./fly.sh buildonly //src/agent/tests:worker_agent_test //src/network/tests:message_protocol_test`）
- ✅ C++ 集成测试 `IdxLoadTest.MergeObjectEndToEnd` 通过
- ✅ 4 个消息 round-trip 测试通过（`DeleteDataMessage` / `DeleteDataAck` 成功+失败路径 / `MergeCleanupMessage`）
- ✅ 3 个 QA 测试通过（`test_merge_db` / `test_merge_db_then_read` / `test_merge_db_waits_for_tasks`）

> 通过 ≠ 无缺陷：当前测试在快机上能稳定跑过，但问题 1/2 是"概率性正确"，慢机器/网络抖动下会偶发失败。

---

## 问题汇总

| # | 严重度 | 状态 | 标题 |
|---|--------|------|------|
| 1 | 中 | OPEN | 删源 `DeleteDataAck` 整套等待机制是死代码，Python 用 `sleep(1.0)` 兜底 |
| 2 | 中 | OPEN | `cleanup_after_merge` 广播后用 `sleep(0.5)` 等 worker 处理 |
| 3 | 低 | OPEN | `restore_master_idx` 在 Phase 3 提前 `mark_data_ready` 是"先污染再清理"绕路 |
| 4 | 低 | OPEN | 三个文件末尾缺换行符（与 codebase 惯例不一致） |
| 5 | 低 | OPEN | C++ 集成测试注释与实现不符（提到已弃用的 `register_write_with_master`） |
| 6 | 观察 | OPEN | `local_workers` 参数语义未在 docstring 点明（已存在则不补齐到 N） |

---

## 问题 1（中等）：删源 `DeleteDataAck` 整套等待机制是死代码

### 定位

- **定义**: `src/agent/cpp/master_agent.h:236-242`（`PendingDeleteData` 结构 + `pending_delete_acks_` map + `delete_ack_mutex_` + `delete_ack_cv_`）
- **登记**: `src/agent/cpp/master_agent.cpp:2210-2213`（`send_delete_data` 开头登记 pending）
- **handler**: `src/agent/cpp/master_agent.cpp:2237-2253`（`on_delete_data_ack`，标记 `completed_` + `notify_all`）
- **死路**: 没有任何 `wait_*_delete_data_ack` 方法；`agent_export.cpp` 未暴露查询接口；Python 从不读取 ack 状态

### 证据

```bash
$ grep -rn "wait.*delete\|delete.*wait\|pending_delete_acks_" src/agent/export/ src/agent/py/
# （无输出 —— export 层和 Python 层完全不引用 ack 等待）
```

Python 编排层实际做法（`src/agent/py/agent.py:535-538`）：

```python
self._agent.send_delete_data(source_worker, db_id, merge_base_path, writer_ids)
INFO(...)
# 给删源 ack 一点时间（删源是 fire-and-forget，不阻塞返回）。
time.sleep(1.0)
```

设计文档 [`db-merge-design.md`](../db-merge-design.md) §3 Phase 5 明确要求"等待所有 DeleteDataAck"，实现未兑现。

### 风险

1. **概率性正确**：删源是 fire-and-forget + 盲等 1 秒。worker 处理慢或 ack 丢失时，`merge_db` 已返回，但源 `.dat` 可能还没删/删除失败 → 用户拿到的"合并库"对应源数据残留，不是真正的自包含。
2. **违反项目硬规则**：`AGENTS.md` 明确 "Zero tolerance for flaky tests — no `sleep(); assert()`"。慢机器上 1 秒不够，`test_merge_db` 的 `assert src_dat_after == 0`（`qa/storage/test_merge_db.py:88`）会偶发失败。
3. **内存泄漏**：`pending_delete_acks_` 每个 entry 在 `on_delete_data_ack` 里只标记 `completed_`，从不 erase（设计上要等 wait 完成才 erase，但 wait 不存在）。多次 merge 后 map 无限增长。

### 建议

补一个等待方法替换 sleep：

```cpp
// master_agent.h
bool wait_delete_data_acks(const CMVector<uint64_t>& source_worker_ids,
                            const CMString& db_id,
                            int64_t timeout_seconds);
// master_agent.cpp：仿 wait_merge_tasks_complete 用 delete_ack_cv_ 等待，
// wait 返回后 erase 对应 ack_key。
```

暴露到 `agent_export.cpp`，Python 层用它替换 `time.sleep(1.0)`：

```python
ok = self._agent.wait_delete_data_acks(source_worker_ids, db_id, 60)
if not ok:
    WARN(f"merge_db: some source delete acks timed out, source .dat may remain")
```

**影响范围**: 改动局限在 `master_agent.{h,cpp}` + `agent_export.cpp` + `agent.py` 三处，与现有 `wait_merge_tasks_complete` 完全对称，低风险。

---

## 问题 2（中等）：`cleanup_after_merge` 广播后用 `sleep(0.5)` 等 worker

### 定位

`src/agent/py/agent.py:546-548`：

```python
self._agent.cleanup_after_merge(...)
INFO("merge_db: cleanup_after_merge done (broadcast + master state rebuilt)")
# 给 worker 处理 MergeCleanup 一点时间。
time.sleep(0.5)
```

`cleanup_after_merge`（`master_agent.cpp:2273-2329`）广播 `MergeCleanupMessage` 是 fire-and-forget，无 ack。

### 风险

- `test_merge_db_then_read` 当前能过，是因为 task 提交+调度本身耗时 > 0.5s，掩盖了竞态。
- 在"merge 后立即有其他 task 调度"的场景下，worker 可能还在清旧 `local_idx_` 的中间态时就开始服务新读请求。
- merge task 完成后 master `remote_idx` 已指向 merge worker，返回的 `merged_db` 立即 read 走 remote_idx 是健壮的（这点设计正确）——所以问题 2 的影响面比问题 1 小，主要是状态清理的时序确定性。

### 建议

借问题 1 的删源 ack 同步点：把 MergeCleanup 广播设计成"最后一个 DeleteDataAck 收到后再发"，或给 MergeCleanup 也加 ack。最简方案是**先解决问题 1**（删源 ack 等待），然后让 cleanup 在删源 ack 全部收到后执行——这样 `sleep(0.5)` 可以直接删除（删源 ack 已保证 worker 在线且响应，cleanup 广播紧随其后时序确定）。

---

## 问题 3（低）：`restore_master_idx` 提前 `mark_data_ready` 是绕路

### 定位

`src/agent/cpp/master_agent.cpp:1655-1675`：Phase 3 读源 idx 时调用 `restore_master_idx`，它内部会：

```cpp
DataService::instance()->restore_entries(db_id, entries);   // 灌入 master local_idx_
for (const auto& entry : entries) {
    graph_->mark_data_ready(entry.object_name_);            // 标记数据就绪
}
```

但 master 进程并不持有这些 `.dat`（在源 worker 本地盘），灌入的 `local_idx_` entry 是无效的。作者用 Phase 5 的 `clear_local_index_for_db` 兜底擦掉（`master_agent.cpp:2295`，注释也说明了）。

### 风险

不致命，是"先污染再清理"的绕路。`restore_master_idx` 本是为 `load_db` 设计的（load 场景 master 确实要建立 local 视图），merge 场景其实只需要 entries 列表来派发 task。

### 建议（可选）

merge 用一个只读 idx、不调 `restore_entries` / `mark_data_ready` 的轻量路径，例如新增 `read_idx_entries(base_path, writer_id)` 只返回 entries 列表。这样 Phase 5 就不需要 `clear_local_index_for_db` 兜底。属于代码整洁度改进，不影响正确性。

---

## 问题 4（低）：文件末尾缺换行符

### 定位

以下三个文件 diff 末尾为 `\ No newline at end of file`：

- `src/agent/cpp/master_agent.cpp`
- `src/agent/cpp/worker_agent.cpp`
- `src/network/tests/message_protocol_test.cpp`

### 风险

无功能影响。codebase 其它文件多数有末尾换行，不一致；部分工具（POSIX 文本文件定义、diff 友好性）期望末尾换行。

### 建议

文件末尾补一个换行符。

---

## 问题 5（低）：C++ 集成测试注释与实现不符

### 定位

`src/agent/tests/worker_agent_test.cpp`，`MergeObjectEndToEnd` 中段注释：

> // __merge_object 通过 register_write_with_master 登记给 master，但 worker2 本地
> // local_idx 的登记依赖 write_record 后的 DataService 状态。此处校验落盘文件即可...

### 风险

实现明确"不调 register_write_with_master"（db 已 freeze 会被拒），走的是 `on_task_complete` 的 internal 分支 `update_remote_idx`。注释是旧设计残留，会误导读者以为 merge task 用了 register_write_with_master 路径。

### 建议

把注释改为描述实际路径：`__merge_object` 通过 `TaskComplete(is_internal_=true)` 回报，master 的 `on_task_complete` internal 分支调 `update_remote_idx` 登记对象位置。

---

## 问题 6（观察）：`local_workers` 参数语义未点明

### 定位

`src/agent/py/agent.py:446-472`：

```python
if not master_host_workers:
    for _ in range(max(1, local_workers)):
        self._spawn_process_worker(...)
```

仅当 master host **无**同 host worker 时才拉起，且只拉一次。若已有 1 个 master host worker，即使 `local_workers=4` 也只用那 1 个（不补齐到 N）。

### 风险

无功能问题。但调用方可能误以为 `local_workers` 能控制并发度，实际它只是"无 worker 时的拉起数量上限"。

### 建议

在 `merge_db` docstring 和 `fly.merge_db` 的 docstring 里点明："仅当 master host 无同 host worker 时拉起 `local_workers` 个；已存在则不补齐，使用现有 worker 数作为并发度。"

---

## 附：做得好的部分（值得保留的模式）

记录以下设计决策，供后续工作参考：

1. **双路径契约利用得当**：识别出 idx 在共享 `base_path`（零搬迁）、只有 `.dat` 需要跨机搬运，复用 backup 范式（`read_raw_compressed` + DataServer TCP）。没有为搬索引发明新消息，是最经济的解法。
2. **消息层干净**：3 个新消息（42/43/44）字段精简、注释充分，`is_valid_message_type` 上界同步更新，round-trip 测试覆盖正常 + 失败路径。
3. **关键决策有据**：方案 B（独立 DataWriter，不构造 Database）避免 `db_paths_` 全局状态污染；只登记 `last_entry` 避免源 idx 历史 entry 误导；cleanup 用 `merge_task_states_` 精确重建 `remote_idx`（object→实际持有 worker）而非全量 target worker。全部在 commit message / 设计文档 §5 记录权衡。
4. **失败语义正确**："全部成功才统一删源"保证 merge 部分失败时源完整保留、可幂等重试。
5. **测试分层到位**：C++ 集成测试验证原语链路，QA 验证端到端 + 阻塞语义 + 前置等待限制。

---

## 修复优先级

1. **必修**：问题 1（补 delete ack 等待，去掉 `sleep(1.0)`）—— 正确性 + flaky 风险。
2. **建议**：问题 2（借问题 1 同步点去掉 `sleep(0.5)`）。
3. **可选清理**：问题 3 / 4 / 5 / 6。
