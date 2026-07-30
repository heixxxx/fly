# Message 日志系统 — Code Review 记录

> 审查对象：`docs/message-system.md` 设计文档 + 本次改动（15 改 + 17 增）。
> 审查范围：设计-实现一致性、并发安全、需求覆盖度、测试盲区。
> 审查日期：2026-07-30。

---

## 0. 审查范围

| 维度 | 审查文件 |
|------|---------|
| 核心模块 | `src/message/cpp/{message_registry,message_sink,message_macros,message_dispatch}.{h,cpp}` |
| 网络 | `src/network/cpp/message_types.h`（+3 enum/struct）、`message_protocol_test.cpp`（+3 round-trip） |
| Agent | `master_agent.{h,cpp}`、`worker_agent.{h,cpp}`（handler / summary 屏障 / push 绑定） |
| 注入 | `src/build_info/BUILD`（genrule）、`src/core/cpp/system_info.{h,cpp}` |
| 导出 | `src/message/export/{BUILD,message_export.cpp}`、`src/fly/__init__.py`、`main.cpp`、`BUILD` |
| 单测 | `src/message/tests/message_registry_test.cpp`（11 例） |
| QA | `qa/message/`（9 例 + `_msgtest.py`） |
| 行为变更 | `src/log/cpp/logger.cpp`（`dual_output_ = false`）、`logger.h`（`format_log`） |

---

## 1. 总体评价

架构清晰、关键设计决策扎实。

- **三层组件职责分明**：`MessageRegistry`（进程级白名单 + 两套触发计数 + id/domain 配额）/ `MessageSink`（master 专属汇聚打印 + 独立打印配额 + summary）/ `message_dispatch`（全局推送函数指针桥接）。
- **核心设计正确**：master 收 worker 推送时用 `MessageSink::print_within_limit`（独立 `print_counts_`），**不调** `MessageRegistry::try_consume`，从而正确避免 summary 双算（worker 已计触发）。这是整个系统最微妙也最关键的一点，实现是对的。
- **配额语义完整**：计数与配额分离（先 +1 再判超限）、两层配额任一超限即丢弃但仍计数、-1/0/N 边界正确（见 `message_registry_test.cpp` 11 例全覆盖）。
- **summary 屏障复刻 MergeCleanupAck**，30s 超时容错合理。
- **测试质量较高**：单测覆盖配额/级别/边界，QA 覆盖多 worker 合并、master/worker 两套配额、FLY::0000 豁免。

但发现 **1 个设计-实现偏差** 与 **1 个线程安全缺陷**，需处理后才能视为「满足需求」。

---

## 2. 🔴 问题 1：master 自身 message 完全不受配额控制（设计-实现偏差）

**严重度：必须修。** 与设计文档明确承诺直接矛盾，且违背配额初衷。

### 2.1 文档承诺

`docs/message-system.md` §3「配额作用的两个维度」：

> 2. **master 打印配额**（master 进程的 `MessageSink`，独立于 `MessageRegistry`）：
>    - 控制推送到 master 的 message（**worker 推来的 + master 自身的**）是否在 master 侧打印（`message.log` + terminal）。

文档明确说 master 打印配额控制「worker 推来的 **+ master 自身的**」。

### 2.2 实际实现

`MessageSink::handle_local`（master 自身 message 的落地路径）**直接 `write_line`，没有调用 `print_within_limit`**：

```cpp
// src/message/cpp/message_sink.cpp:74
void MessageSink::handle_local(LogLevel level, const CMString& domain_id, int32_t source, const CMString& msg) {
    CMString line = "[" + timestamp() + "] [" + level_str(level) +
                    "] [master] [" + domain_id + "] <" + std::to_string(source) + "> " + msg + "\n";
    write_line(line);   // ← 没有 print_within_limit！bypass master 打印配额
}

// src/message/cpp/message_sink.cpp:111（对比：worker 推来的走配额）
bool MessageSink::handle_remote(uint64_t worker_id, ...) {
    if (!print_within_limit(domain_id)) {   // ✓ worker 推送的受控
        return false;
    }
    ...write_line(line);
}
```

绑定关系（`master_agent.cpp:53-60`）：
- master 的 `set_message_push_func` → `MessageSink::handle_local`
- master 的 `set_system_sink_func` → `MessageSink::handle_local`

两条 master 侧路径都落到 `handle_local`，都 bypass 配额。

### 2.3 后果

- `set_master_print_id_limit` / `set_master_print_domain_limit` **对 master 自身 message 完全无效**。
- master 自身（含 FLY::0000 之外的 message，以及未来 master 跑 solver 任务时）高频发 message 会直接刷爆 terminal / 膨胀 message.log——正是配额要解决的「避免 terminal 刷屏」问题（§1 目标 4）。

### 2.4 为什么 QA 没抓到

`qa/message/test_message_master_quota.py` 只验证了 **worker 推送**（worker 发 5 条，master 配额=3，断言 message.log 收 3 条）。**没有测试 master 自身 message 是否同样受 master 打印配额约束**——测试盲区。

### 2.5 建议（二选一，推荐前者）

1. **修复实现**：`handle_local` 也调 `print_within_limit`，使 master 自身与 worker 推送共用同一套 master 打印配额（与文档 §3 承诺一致）。并补 1 个 QA 用例：master 自身发 N 条、master 配额设 < N，验证 message.log 只落配额内条数。
2. **改文档**：明确「master 自身 message 豁免 master 打印配额」。但这与配额初衷矛盾，且当前导出 API `set_master_print_*` 的语义会变得割裂，**不推荐**。

> 注：若采用方案 1，需同时复核 FLY::0000 系统信息（`emit_system_message` → system_sink → `handle_local`）。FLY::0000 设计上豁免配额（§5.1），而 `emit_system_message` 在上游就已 bypass `try_consume`。若 `handle_local` 加配额会误伤 FLY::0000。处理方式：给 `handle_local` 加一个 `bool honor_quota` 形参，系统信息路径传 `false`，普通 message 路径传 `true`；或将系统信息单独走一个不受配额的写入方法。

---

## 3. 🟠 问题 2：`std::localtime` 多线程不安全（data race / UB）

**严重度：强烈建议修。** 属于「零容忍稳定性」范畴（AGENTS.md 要求立即修复所有 crash/UB 风险）。

### 3.1 位置

```cpp
// src/message/cpp/message_sink.cpp:42-51  MessageSink::timestamp()
CMString MessageSink::timestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    ...
    ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
    //                   ^^^^^^^^^^^^^^^^^^ 返回 static tm*，非可重入
```

### 3.2 为什么是 UB

Reactor 是**多线程**模型（`src/network/cpp/reactor.h:32` `CMVector<std::thread> workers_`，`HandlerThreadPool` 启动多 worker 线程）。master 进程上：

- 各 reactor worker 线程并发跑 `on_log_message` → `MessageSink::handle_remote` → `timestamp()`。
- 同时 master 主线程 / task 线程经 `handle_local` → `timestamp()`。

`timestamp()` 在 `write_line` 持有的 `mutex_` **之外**被调用（`message_sink.cpp:74-78`、`111-122`），故 `std::localtime` 内部 static buffer 的并发访问构成 data race，属于未定义行为。运行环境为 WSL2/Linux gcc12，在并发压力下可能出现时间戳错乱、偶发崩溃，难以复现。

### 3.3 建议

改用 POSIX 可重入版本 `localtime_r`：

```cpp
CMString MessageSink::timestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()) % 1000;
    struct tm tm_local;
    localtime_r(&time, &tm_local);   // 可重入，无 static buffer
    std::stringstream ss;
    ss << std::put_time(&tm_local, "%Y-%m-%d %H:%M:%S");
    ss << "." << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}
```

相关位置（一并处理更一致）：
- `src/core/cpp/system_info.cpp:128` `now_str()` 同样用 `std::localtime`——启动时基本单线程，风险低，但建议顺手修。
- `src/log/cpp/logger.cpp:126` 既有代码同模式，可作为后续清理项。

---

## 4. 次要观察（非阻塞，可后续处理）

| # | 位置 | 观察 | 建议 |
|---|------|------|------|
| 4.1 | 全仓 | grep 仅 `_fly_message` export 与 QA 调用 `register_message_id`，**无任何 solver/storage 等业务模块注册** message id。系统目前是空载基础设施，只有 FLY::0000 和测试用例在用。 | 文档 §12 已声明为后续工作。作为基础设施 PR 可接受，但需确认本轮交付范围确实只是框架。 |
| 4.2 | `src/message/export/message_export.cpp:7-11` `parse_level` | 非法级别字符串（如 `"BOGUS"`）静默降级为 `INFO`，不报错。 | 轻微。若想严格可在 `register_message_id` 对非法 level 抛错或记 WARN。 |
| 4.3 | `master_agent.cpp` `collect_and_print_message_summary` | summary 屏障：若某 worker 在 `worker_to_conn_` 快照后断连，`reactor_->send` 失败无反馈，master 等满 30s 超时才用部分计数打印 summary。 | 复刻 MergeCleanupAck 的既有容错策略（§10.7），可接受。30s 对开发体验略长，如需可参数化缩短。 |
| 4.4 | `message_sink.cpp:29` `std::ios::app` | `message.log` append 模式，多次启动累积。 | 与 Logger 一致，runqa 清理日志目录，无实际问题。 |

---

## 5. 需求覆盖度核对

依据 `docs/message-system.md` §1 核心目标：

| 需求 | 覆盖 | 说明 |
|------|------|------|
| 高价值信息远程推送（worker → master → terminal/message.log） | ✅ | `LogMessage` + `WorkerAgent::send_message_to_master` + `MasterAgent::on_log_message` 链路正确。 |
| 不干扰 debug log | ✅ | debug log 完全保留，message 复用 `Logger::log` 写本地 debug log（带 `[DOMAIN::NNNN] <source>` 前缀），研发可见。 |
| terminal 行为变更（master debug log 不进 terminal） | ✅ | `logger.cpp` `dual_output_ = false`。terminal 唯一来源是 `MessageSink`。 |
| 两层配额（domain/id） | ✅ worker 侧 / ⚠️ master 侧 | worker 本地 `MessageRegistry` 两层配额正确；**master 自身 message 漏配额（问题 1）**。 |
| summary | ✅ | 屏障 + 合并逻辑正确，无双算；id 级 + domain 级聚合；空触发有 `(no message triggered)` 分支。 |
| FLY::0000 启动信息 | ✅ | 4 分组、关键字段、build_info 注入（genrule `local=True`）、豁免配额、worker 不发送，QA 全验证。 |

---

## 6. 结论

实现整体**简洁、健壮，架构选择（尤其避免双算的设计）经得起推敲**。需修复两点后方可视为「满足需求」：

1. **必须修 — 问题 1**（master 自身 message 配额）：与文档 §3 直接矛盾，违背配额初衷，且当前是测试盲区。修 `handle_local` + 补 1 个 QA 用例。
2. **强烈建议修 — 问题 2**（`localtime_r`）：多进程多线程系统中的 data race / UB，属稳定性零容忍项。

修复这两点后，message 系统作为基础设施是可靠的，可作为「框架就绪」交付，后续各业务模块按需注册 id 即可启用。
