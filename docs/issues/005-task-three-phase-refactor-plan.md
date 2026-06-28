# 005 — Task 三阶段执行重构方案（freeze 延迟可见 + postprocess 重构 + C++ 三阶段编排上移）

> 状态：**设计已确认（grill-me 通过，Q1-Q5 全部闭环）**，待实施
> 范围：worker task 执行链路、freeze 通知、write register 可见性、Python/C++ 三阶段编排边界
> 设计原则：**非 stream 模式 = task 级原子性**（即时校验 + 延迟可见 + 整体回滚）；**C++ 提供原语，Python 负责调度**
> 关键决策：pending frozen 按 task_id 清理（覆盖崩溃失败）+ freeze 加 ack 通道（冲突 fail-fast）+ drain 移入 postprocess + 三阶段编排上移 Python

---

## 0. 背景与问题

当前 task 执行存在三组互相耦合的设计缺陷：

### 问题 1：freeze 通知双路径冗余且语义错乱
- **路径 A（即时）**：`Database::freeze()`（`database.cpp:392`）末尾 `notify_freeze` → worker `set_freeze_func` 回调 → `request_database_freeze` → 即时发 `DatabaseFreezeNotification` → master `on_database_freeze_request`（`master_agent.cpp:1714`）登记 + 本地 freeze + **全网广播**。
- **路径 B（延迟）**：Python executor 编排闭包（`executor.py:168-194`）算 frozen 差集 → 塞入 `TaskCompleteMessage.frozen_dbs_` → master `on_task_complete`（`master_agent.cpp:802-821`）**再登记 + 再 freeze + 再广播一次**。

后果：同一次 freeze，master 和所有 worker 收到两次 `DatabaseFreezeNotification`；Python 侧为此维护跨 task 复用的 `_db_cache` 并遍历两次拍快照做差集，引入了不必要的复杂度。差集计算本身也是错的——freeze 是 task 内的主动行为，应由行为发生时显式登记，而非靠前后快照推断。

### 问题 2：非 stream 模式下 write register 的可见性未延迟
当前无论 stream / 非 stream 模式，worker 每次 `write_object` 都即时发 `WriteRegisterMessage`，master 的 `do_write_register`（`master_agent.cpp:1086`）在 ack 成功路径里**无条件** `mark_data_ready` + `update_remote_idx` + `schedule_tasks`（`:1120-1128`）。

但 `on_task_complete`（`:788-796`）只在 `!streaming_mode` 时才对 `written_objects_` 做 `mark_data_ready` + `update_remote_idx`——这意味着**非 stream 模式下数据被注册了两次**（write 时一次、complete 时一次），且 write 时的即时 mark_data_ready 破坏了非 stream 模式应有的 task 级原子性：task 失败回滚后，已 mark_data_ready 的下游 task 可能已被调度执行。

### 问题 3：postprocess 是空壳，"后处理"职责散落
`executor.py:150` 的 `postprocess` 自承认 no-op，但真正的后处理逻辑（`drain_write_back` `:182`、frozen 差集 `:184-187`）散落在编排闭包里，三阶段名存实亡。

### 问题 4：C++/Python 三阶段编排边界不清
C++ `poll_task`（`worker_agent.cpp:315-400`）硬编码了 `begin_task → execute → end_task → (commit|cleanup)` 的事务编排，Python executor 只做 `preprocess → execute → postprocess`。两套并行生命周期，命名不对称（`begin/end` vs `pre/post`），职责重叠（drain 被调两次、var 被搬两次）。

---

## 1. 设计共识（grill-me 已确认）

### 1.1 核心原则：两种模式的数据可见性语义

| 维度 | stream 模式（`dependency_update_mode==0`，默认） | 非 stream 模式（`!=0`） |
|---|---|---|
| write register 校验 | 即时发，master 即时 provenance + frozen 检查 + ack | **同左**（即时校验，失败→task 失败） |
| write register 可见性 | master 即时 `mark_data_ready` + `update_remote_idx` | **延迟**到 `on_task_complete` 统一注册 |
| freeze 本地状态 | task 内 `db.freeze()` 即时（worker 本地 frozen + marker） | **同左** |
| freeze 通知路径 | 路径 A：即时 `DatabaseFreezeNotification` | **路径 B**：延迟到 `TaskCompleteMessage.frozen_dbs_`（显式登记） |
| frozen 检查（跨 task） | master `is_db_frozen`（已确认集合） | master `is_db_frozen`（已确认）**或** `is_pending_frozen`（待确认） |

**关键不变量**：非 stream 模式下，所有对 master 的可见副作用（write 的 mark_data_ready、freeze 的广播）都延迟到 task **成功完成**；但**即时校验**（provenance + frozen）不延迟，校验失败则整体回滚（已完成的写 + pending freeze 一起回滚）。

### 1.2 freeze 状态机（三态，方案 A）

master 侧引入 **pending frozen** 中间态，让 freeze 也具备"事务"语义：

| 时刻 | `frozen_dbs_`（已确认） | `pending_frozen_dbs_`（新增，待确认） | master 本地 db | 其他 worker |
|---|---|---|---|---|
| task 内 `db.freeze()` | 不变 | **insert** | 不变 | 不感知 |
| 同 task 内后续 write 到该 db | — | — | worker 本地 `check_frozen` 拦截（`database.cpp:215/227`） | — |
| 其他 task write register 到该 db | — | 命中 → **拒绝**（`WRITE_TO_FROZEN_DB`） | — | — |
| **task 成功** | pending 项 **迁移**至此 + 广播 + 本地 freeze | erase | — | 收广播后本地 freeze |
| **task 失败** | 不变 | **erase**（回滚） | — | 不感知 |

**关键点**：
- pending frozen 只记 `db_id`（不记 task 归属）；同 db 被多个 task 尝试 freeze，第二个被当"已冻结"拒绝。
- 非 stream 模式下 freeze **不广播**直到 task 成功；stream 模式保持路径 A 即时广播。
- `is_db_frozen` 检查同时覆盖两个集合：`frozen_dbs_ ∪ pending_frozen_dbs_`。

### 1.3 C++/Python 编排边界

- **C++ 提供原语**：`begin_task` / `end_task` / `commit_task_segments` / `cleanup_failed_task_writes` / `drain_write_back`（已有或待导出）。
- **Python 负责调度**：executor 闭包编排三阶段调用顺序与 commit/cleanup 分支决策。
- **C++ 仍负责发消息**：`TaskCompleteMessage` / `TaskFailedMessage` 的构造与发送留在 C++ `poll_task`（reactor 在 C++，Python 不直接持有 `master_conn_`）。
- **决策上移、原语下沉**：减少跨语言调用面，符合 fly 设计原则。

---

## 2. 改动清单（逐文件）

### 改动分三个独立可验证的工作包：
- **WP1 — freeze 延迟可见（方案 A）**：解决问题 1 的非 stream 模式部分 + 差集消除。
- **WP2 — write register 可见性延迟**：解决问题 2（与 WP1 同属"非 stream 原子性"主题）。
- **WP3 — postprocess 重构 + 三阶段编排上移**：解决问题 3、4。

> WP1/WP2 涉及 stream 与非 stream 模式分支，必须保证 **stream 模式行为完全不变**（默认配置 `dependency_update_mode==0` 是 stream）。

---

### WP1：freeze 延迟可见

#### WP1-1. `src/agent/cpp/master_agent.h`
新增 pending frozen 映射与查询（pending 记 `task_id`，支持崩溃恢复按 task_id 清理）：

```cpp
// :158 附近，与 frozen_dbs_ 并列
CMUnorderedSet<CMString> frozen_dbs_;                       // 已确认冻结（task 完成后）
CMUnorderedMap<CMString, uint64_t> pending_frozen_dbs_;     // 【新增】db_id → task_id（待确认）
mutable std::mutex frozen_dbs_mutex_;                       // 同时保护两个结构
```

```cpp
// :84 附近，查询与清理
bool is_db_frozen(const CMString& db_id) const;             // 已有，改为查 并集（confirmed ∪ pending）
bool is_db_pending_frozen(const CMString& db_id) const;     // 【新增】仅查 pending
void commit_pending_frozen(uint64_t task_id);               // 【新增】task 成功：pending→confirmed 迁移 + 广播
void rollback_pending_frozen(uint64_t task_id);             // 【新增】task 失败/崩溃：按 task_id 清 pending
```

> **为什么 pending 记 task_id（Q1 决策）**：task 失败分两种——可恢复失败（能发 `TaskFailedMessage`）和崩溃失败（worker SIGSEGV/断网，**收不到失败消息**）。按 task_id 清理才能覆盖崩溃场景：`on_disconnect` 通过 `metadata_->get_task_ids_by_worker()` 拿到崩溃 worker 的全部 task，按 task_id 批量清 pending。若按 db_id 靠 failed 消息携带（选项 a），崩溃时 master 永远收不到该列表 → pending 永久残留 → 该 db 被永久标"冻结中" → 后续所有写被拒 → **死锁级 bug**。

#### WP1-2. `src/agent/cpp/master_agent.cpp`

**`is_db_frozen`（`:1018`）改为查并集**：
```cpp
bool MasterAgent::is_db_frozen(const CMString& db_id) const {
    std::lock_guard<std::mutex> lk(frozen_dbs_mutex_);
    return frozen_dbs_.count(db_id) > 0 || pending_frozen_dbs_.count(db_id) > 0;
}
```

**新增 `DatabaseFreezeAckMessage`（Q1 决策：freeze 加 ack 通道，方案 P）**：
freeze 当前是 fire-and-forget（`worker_agent.cpp:567` 无 ack），无法把"冲突"反馈给 worker。改为同步 ack 模式（仿 write register），让冲突即时联动 task 失败。

`message_types.h` 新增：
```cpp
struct DatabaseFreezeAckMessage {
    MessageHeader header_;
    CMString db_id_;
    bool success_ = true;
    TaskErrorType error_type_ = TaskErrorType::UNKNOWN;
    static constexpr MessageType msg_type_ = MessageType::DATABASE_FREEZE_ACK;
    FLY_SERIALIZE(header_, db_id_, success_, error_type_);
};
```
`MessageType` 枚举新增 `DATABASE_FREEZE_ACK`。

**新增错误类型 `DB_ALREADY_FROZEN`**（`error_types.h`）：区分"写已冻结 db"（`WRITE_TO_FROZEN_DB`）与"冻结已冻结/正在冻结的 db"（`DB_ALREADY_FROZEN`），语义更准确。

**`on_database_freeze_request`（`:1714`）按模式分流 + 冲突检测**：
```cpp
void MasterAgent::on_database_freeze_request(uint64_t conn_id, const DatabaseFreezeNotification& msg) {
    bool streaming_mode = (Config::instance()->get_int("dependency_update_mode") == 0);
    DatabaseFreezeAckMessage ack;
    ack.db_id_ = msg.db_id_;

    {
        std::lock_guard<std::mutex> lk(frozen_dbs_mutex_);
        bool already = frozen_dbs_.count(msg.db_id_) > 0 || pending_frozen_dbs_.count(msg.db_id_) > 0;
        if (already) {
            // 冲突：db 已冻结或正在被其他 task 冻结（业务流程错误）→ fail-fast
            ack.success_ = false;
            ack.error_type_ = TaskErrorType::DB_ALREADY_FROZEN;
            WARN("Freeze rejected (already frozen/pending): db_id={}", msg.db_id_);
        } else if (streaming_mode) {
            // stream：即时确认（保持现状语义）
            frozen_dbs_.insert(msg.db_id_);
            ack.success_ = true;
        } else {
            // 非 stream：登记 pending（记 task_id）
            // 注：task_id 由 worker 在 DatabaseFreezeNotification 中携带（见 WP1-3）
            pending_frozen_dbs_[msg.db_id_] = msg.task_id_;
            ack.success_ = true;
        }
    }
    reactor_->send(conn_id, ack);   // ★ 同步 ack（两种模式都回）

    // stream 模式的广播移到 ack 之后（保持原 :1732-1738 语义）
    if (streaming_mode && ack.success_) {
        auto it = db_instances_.find(msg.db_id_);
        if (it != db_instances_.end()) it->second->freeze();
        DatabaseFreezeNotification broadcast_msg = msg;
        std::lock_guard<std::mutex> wlk(workers_mutex_);
        for (const auto& [wid, cid] : worker_to_conn_)
            reactor_->send(cid, broadcast_msg);
    }
}
```

> **DatabaseFreezeNotification 需新增 `task_id_` 字段**：非 stream 模式下 master 登记 pending 要记 task_id，由 worker 在通知里携带。worker 侧 `request_database_freeze` 已知当前 task（`current_task_id_`，begin_task 时设置）。

**新增 `commit_pending_frozen` / `rollback_pending_frozen`**（task 完成/失败时调用）：
```cpp
void MasterAgent::commit_pending_frozen(uint64_t task_id) {
    // task 成功：该 task 的 pending 项 → confirmed + 广播
    CMVector<CMString> committed;
    {
        std::lock_guard<std::mutex> lk(frozen_dbs_mutex_);
        for (auto it = pending_frozen_dbs_.begin(); it != pending_frozen_dbs_.end(); ) {
            if (it->second == task_id) {
                frozen_dbs_.insert(it->first);
                committed.push_back(it->first);
                it = pending_frozen_dbs_.erase(it);
            } else { ++it; }
        }
    }
    // 广播（task 成功后才广播）
    for (const auto& db_id : committed) {
        auto it = db_instances_.find(db_id);
        if (it != db_instances_.end()) it->second->freeze();
        DatabaseFreezeNotification broadcast_msg; broadcast_msg.db_id_ = db_id;
        std::lock_guard<std::mutex> wlk(workers_mutex_);
        for (const auto& [wid, cid] : worker_to_conn_) reactor_->send(cid, broadcast_msg);
    }
}

void MasterAgent::rollback_pending_frozen(uint64_t task_id) {
    // task 失败/崩溃：按 task_id 清 pending（worker 本地 reset 由失败处理流程负责）
    std::lock_guard<std::mutex> lk(frozen_dbs_mutex_);
    for (auto it = pending_frozen_dbs_.begin(); it != pending_frozen_dbs_.end(); ) {
        if (it->second == task_id) {
            WARN("Rolling back pending freeze: db_id={}, task_id={}", it->first, task_id);
            it = pending_frozen_dbs_.erase(it);
        } else { ++it; }
    }
}
```

**`on_task_complete`（`:802-821`）的 frozen 处理块改为调 `commit_pending_frozen`**：
```cpp
// 原 :802-821 整块替换为：
commit_pending_frozen(msg.task_id_);   // 该 task 的 pending → confirmed 迁移 + 广播
```
> `TaskCompleteMessage.frozen_dbs_` 字段可保留用于校验/日志，但 master 不再依赖它做迁移——迁移按 `task_id` 从 `pending_frozen_dbs_` 精确提取。这样即使 complete 消息丢失/篡改，pending 也不会被错误提交。

**`on_task_failed`（`:840`）新增 pending 回滚**：
```cpp
// 在 on_task_failed 末尾、schedule_tasks() 之前
rollback_pending_frozen(msg.task_id_);   // 清掉该 task 声明的 pending freeze
```

**`on_disconnect`（`:920-948`）崩溃恢复新增 pending 清理**（Q1 关键：覆盖崩溃失败场景）：
```cpp
auto tasks_to_recover = metadata_->get_task_ids_by_worker(worker_id);

// 【新增】崩溃 worker 声明的 pending frozen 必须清理，否则该 db 被永久标"冻结中" → 死锁
for (uint64_t tid : tasks_to_recover) {
    rollback_pending_frozen(tid);
}
```
> 这是 Q1 选 task_id 而非 db_id 的核心理由：崩溃时 master 收不到任何失败消息，只能靠 `on_disconnect` 的 worker→task 反查（`get_task_ids_by_worker`）按 task_id 批量清理。

#### WP1-3. `src/agent/cpp/worker_agent.cpp` — freeze 改同步 ack + 显式登记

**`request_database_freeze`（`:567`）从 fire-and-forget 改为同步 ack**（Q1 方案 P，仿 `register_write_with_master` 的 pending+cv 模式）：

新增成员（`worker_agent.h`）：
```cpp
CMVector<CMString> current_task_frozen_dbs_;                          // 【新增】本 task freeze 的 db_id（显式登记，取代差集）
std::mutex pending_freeze_mutex_;                                     // 【新增】
std::condition_variable pending_freeze_cv_;                           // 【新增】
CMUnorderedMap<CMString, CMSharedPtr<PendingFreezeAck>> pending_freezes_;  // 【新增】db_id → 待 ack
```
其中 `PendingFreezeAck` 仿 `PendingWriteRegister`（`worker_agent.h:41`）：`db_id_ / completed_ / success_ / error_type_`。

```cpp
void WorkerAgent::request_database_freeze(const CMString& db_id) {
    if (!registered_) return;

    // 登记 per-task 列表（取代 executor.py 差集；两种模式都需要，供 complete 消息显式带上）
    if (std::find(current_task_frozen_dbs_.begin(), current_task_frozen_dbs_.end(), db_id)
        == current_task_frozen_dbs_.end()) {
        current_task_frozen_dbs_.push_back(db_id);
    }

    // 同步等 ack（Q1 冲突检测的反馈通道）
    auto pending = CMMakeShared<PendingFreezeAck>();
    pending->db_id_ = db_id;
    {
        std::lock_guard<std::mutex> lock(pending_freeze_mutex_);
        pending_freezes_[db_id] = pending;
    }

    DatabaseFreezeNotification msg;
    msg.db_id_ = db_id;
    msg.task_id_ = current_task_id_;   // ★ 非 stream 模式 master 登记 pending 需要
    reactor_->send(master_conn_, msg);

    {
        std::unique_lock<std::mutex> lock(pending_freeze_mutex_);
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!pending->completed_) {
            if (pending_freeze_cv_.wait_until(lock, deadline) == std::cv_status::timeout) break;
        }
        pending_freezes_.erase(db_id);
        if (!pending->completed_) {
            // 超时（master 无响应）→ 当失败处理，联动 task 失败
            WorkerAgentContext::set_last_error_type(TaskErrorType::WRITE_REGISTRATION_TIMEOUT);
            ERR("Freeze ack timeout: db_id={}", db_id);
            return;
        }
        if (!pending->success_) {
            // 冲突（DB_ALREADY_FROZEN）→ 联动 task 失败（poll_task 检查 last_error_type）
            WorkerAgentContext::set_last_error_type(pending->error_type_);
            ERR("Freeze rejected: db_id={}, error_type={}", db_id, static_cast<int>(pending->error_type_));
            return;
        }
    }
    INFO("Freeze acked: db_id={}", db_id);
}
```

新增 ack handler（仿 `on_write_register_ack`，`:778`）：
```cpp
void WorkerAgent::on_database_freeze_ack(uint64_t conn_id, const DatabaseFreezeAckMessage& msg) {
    touch_master_contact();
    std::lock_guard<std::mutex> lock(pending_freeze_mutex_);
    auto it = pending_freezes_.find(msg.db_id_);
    if (it != pending_freezes_.end()) {
        it->second->success_ = msg.success_;
        it->second->error_type_ = msg.error_type_;
        it->second->completed_ = true;
    }
    pending_freeze_cv_.notify_all();
}
```
reactor 注册（仿 `:90` 的 WriteRegisterAck 注册）。

- `begin_task`（`:465`）：清空 `current_task_frozen_dbs_`。
- `end_task`（`:519`）：move 出 `current_task_frozen_dbs_` 供 complete 消息填充（取代 `result.frozen_dbs_`）。
- `poll_task` 失败分支：worker 本地对该 task freeze 过的 db 调 `Database::reset()`（`database.cpp:558`）回滚本地 frozen 状态（marker + is_frozen_）。

#### WP1-4. `src/agent/py/executor.py`

**删除差集计算**（`:168`、`:174-176`、`:184-187`、`result['frozen_dbs']`）：
- 不再维护 `_frozen_before` 快照。
- 不再遍历 `_db_cache` 两次。
- `result['frozen_dbs']` 改为由 C++ `poll_task` 从 `current_task_frozen_dbs_` 填充，或直接从 executor 闭包返回值移除该字段（改由 C++ 侧 `end_task` 产出）。

> 注意：freeze 本身仍是 task 内主动行为（`Database::freeze()` 即时执行），只是"通知"和"列表收集"的方式从"差集推断"改为"显式登记"。

#### WP1-5. `TaskCompleteMessage` / `TaskFailedMessage` / `DatabaseFreezeNotification` 字段（`message_types.h`）

**`DatabaseFreezeNotification`（`:173` 附近）新增 `task_id_`**（Q1 已决）：
```cpp
struct DatabaseFreezeNotification {
    MessageHeader header_;
    CMString db_id_;
    uint64_t task_id_ = 0;   // 【新增】非 stream 模式 master 登记 pending 需要
    static constexpr MessageType msg_type_ = MessageType::DATABASE_FREEZE_NOTIFICATION;
    FLY_SERIALIZE(header_, db_id_, task_id_);
};
```
**新增 `DatabaseFreezeAckMessage`**（Q1 方案 P，代码见 WP1-2）。

**`TaskCompleteMessage.frozen_dbs_`（`:184`）**：**保留**，内容来源从差集改为显式登记的 `current_task_frozen_dbs_`。但 master 的迁移逻辑**不依赖该字段**（按 task_id 从 pending 提取），该字段仅用于校验/日志。

**`TaskFailedMessage`**：**不新增字段**（Q1 已决：选 b，master 按 task_id 清理，无需 failed 消息携带 pending 列表）。
- 可恢复失败：`on_task_failed` 用 `msg.task_id_` 调 `rollback_pending_frozen(msg.task_id_)`。
- 崩溃失败：`on_disconnect` 用 `get_task_ids_by_worker()` 对每个 task_id 调 `rollback_pending_frozen`。

---

### WP2：write register 可见性延迟（非 stream 模式）

#### WP2-1. `src/agent/cpp/master_agent.cpp` — 拆分 `do_write_register`

当前 `do_write_register`（`:1086`）把"校验"和"可见性登记"耦合在一起。拆为两段：

```cpp
WriteRegisterAckMessage MasterAgent::do_write_register(const WriteRegisterMessage& msg) {
    WriteRegisterAckMessage ack;
    ack.object_name_ = msg.object_name_;
    ack.db_id_ = msg.db_id_;

    // —— 校验段（两种模式都即时执行，失败→ack 拒绝→task 失败）——
    bool registered_ok = false;
    if (is_db_frozen(msg.db_id_)) {                       // 已覆盖 pending frozen（WP1）
        ack.success_ = false;
        ack.error_type_ = TaskErrorType::WRITE_TO_FROZEN_DB;
        ack.error_message_ = "Database frozen: " + msg.db_id_;
    } else if (!msg.write_context_hash_.empty()) {
        // provenance 检查（原 :1099-1113）
        ...
    } else {
        registered_ok = true;
    }

    if (!registered_ok) return ack;   // 校验失败直接返回，不做可见性登记

    ack.success_ = true;

    // —— 可见性登记段（仅 stream 模式即时执行；非 stream 延迟到 on_task_complete）——
    // Q2 已决：master 自写（worker_id_==0）强制走即时登记，不受 dependency_update_mode 影响
    // —— master 进程无 task 三阶段（不设 transaction_mode、无 TaskCompleteMessage），
    //    没有延迟登记的触发时机，必须即时生效。
    bool streaming_mode = (msg.worker_id_ == 0) ||
                          (Config::instance()->get_int("dependency_update_mode") == 0);
    if (streaming_mode) {
        graph_->mark_data_ready(msg.object_name_);
        auto addr = DataService::instance()->get_worker_address(msg.worker_id_);
        DataService::instance()->update_remote_idx(msg.object_name_, msg.worker_id_,
                                                   addr.host_, addr.port_, msg.size_bytes_);
        update_dependency_location_cache(msg.object_name_, msg.worker_id_, addr.host_, addr.port_);
        record_worker_info(msg.object_name_, msg.db_id_, msg.worker_id_, msg.writer_id_);
        if (msg.worker_id_ == 0 && Config::instance()->get_int("auto_backup_enabled") == 1) {
            evaluate_and_trigger_backup(msg.object_name_, 0, msg.db_id_);
        }
        schedule_tasks();
    }
    // 非 stream 模式：仅 ack 成功，不 mark_data_ready。登记延迟到 on_task_complete 的 written_objects_。

    return ack;
}
```

`on_task_complete`（`:790-796`）的 `!streaming_mode` 分支**保留**（已有逻辑，现在不再重复注册）。

#### WP2-2. worker 侧 `register_write_with_master`（`worker_agent.cpp:702`）—— 保持不变

worker 侧每次 write 仍即时发 `WriteRegisterMessage` 并**同步等 ack**（即时校验）。ack 携带拒绝原因（`WRITE_TO_FROZEN_DB` / `WRITE_PROVENANCE_MISMATCH`），worker 通过 `WorkerAgentContext::set_last_error_type` 记录，`poll_task` 在 execute 后检查 `get_last_error_type()`（已有逻辑 `:340-356`）联动 task 失败 + cleanup。

> 注意 worker 侧的本地 provenance 预检（`:707-732`）保持不变——它是性能优化（避免无效网络往返），与 master 的权威检查互补。

---

### WP3：postprocess 重构 + 三阶段编排上移

#### WP3-1. `src/agent/export/agent_export.cpp` — 导出 C++ 原语给 Python

在 `WorkerAgent` 导出块（`:208` 之后）新增：
```cpp
FLY_EXPORT_METHOD("begin_task", [](fly::WorkerAgent& self, uint64_t task_id, const fly::CMString& h) {
    self.begin_task(task_id, h);
})
FLY_EXPORT_METHOD("end_task", [](fly::WorkerAgent& self) -> fly::CMVector<fly::CMString> {
    return self.end_task(0);   // task_id 参数当前未使用
})
FLY_EXPORT_METHOD("commit_task_segments", [](fly::WorkerAgent& self, const fly::CMVector<fly::CMString>& w) {
    self.commit_task_segments(w);
})
FLY_EXPORT_METHOD("cleanup_failed_task_writes", [](fly::WorkerAgent& self, const fly::CMVector<fly::CMString>& d) {
    self.cleanup_failed_task_writes(d);
})
FLY_EXPORT_METHOD("get_current_writes", [](fly::WorkerAgent& self) -> fly::CMVector<fly::CMString> {
    return self.get_current_writes();   // 【新增】供 Python postprocess 读取 tracked_writes
})
FLY_EXPORT_METHOD("get_current_write_sizes", [](fly::WorkerAgent& self)
        -> fly::CMUnorderedMap<fly::CMString, int64_t> {
    return self.get_current_write_sizes();   // 【新增】供构造 complete.written_objects_
})
FLY_EXPORT_METHOD("get_current_task_frozen_dbs", [](fly::WorkerAgent& self) -> fly::CMVector<fly::CMString> {
    return self.get_current_task_frozen_dbs();   // 【WP1 新增】
})
```

#### WP3-2. `src/agent/cpp/worker_agent.h/.cpp` — 新增 getter

```cpp
// worker_agent.h（public）
CMVector<CMString> get_current_writes() const;                 // 拷贝返回 current_writes_
CMUnorderedMap<CMString, int64_t> get_current_write_sizes() const;  // 拷贝返回
CMVector<CMString> get_current_task_frozen_dbs() const;        // 拷贝返回（WP1）
```
> getter 返回拷贝而非引用，避免跨 GIL/线程生命周期问题（`current_writes_` 在 end_task 后被 move 走）。

#### WP3-3. `src/agent/py/executor.py` — 重构三阶段

```python
def create_executor(worker):
    def preprocess(task_id, task_name, task_module, args, write_context_hash):
        # ① C++ 事务预处理（原 C++ poll_task 的 begin_task + stage vars）
        worker._agent.begin_task(task_id, write_context_hash)
        # ② 业务预处理（原样）
        deserialized_args = _deserialize_args(args, worker)
        pending_vars = worker._agent.take_pending_task_vars()
        if pending_vars:
            ...  # _inject_var 原样
        return deserialized_args

    def execute(task_id, task_name, task_module, deserialized_args):
        original_func = _resolve_func(task_name, task_module)
        return original_func(*deserialized_args)

    def postprocess(task_id):
        # ① 落盘（从编排闭包移入此处）
        from _fly_storage import ex_stg_get_data_service
        ex_stg_get_data_service().drain_write_back()
        # ② C++ 事务后处理：拆除回调 + 收集 tracked_writes
        tracked_writes = worker._agent.end_task()
        return tracked_writes

    def executor(task_id, task_name, task_module, args, write_context_hash) -> dict:
        result = {'task_id': task_id, 'status': 0, 'output': '', 'error': '', 'outputs': []}
        tracked_writes = []
        began = False   # Q5 已决：方案 i，Python try/except 跟踪 begin_task 是否已执行
        try:
            deserialized_args = preprocess(task_id, task_name, task_module, args, write_context_hash)
            began = True                              # begin_task 已执行
            output = execute(task_id, task_name, task_module, deserialized_args)
            tracked_writes = postprocess(task_id)     # 内含 drain + end_task
            began = False                             # end_task 已执行，无需补调
            result['status'] = 0
            result['output'] = str(output) if output is not None else ""
        except Exception as e:
            # 异常路径：若 begin_task 已执行但 postprocess 未执行，必须补调 end_task 拆回调防泄漏
            if began:
                try:
                    tracked_writes = worker._agent.end_task()
                except Exception:
                    pass
                began = False
            result['status'] = 1
            result['output'] = ""
            result['error'] = traceback.format_exc()
            ERR(f"Task execution failed: id={task_id}, name={task_name}, error={e}")
        result['tracked_writes'] = tracked_writes   # 供 C++ 决策 commit/cleanup + 填充消息
        return result
```

**变化要点**：
- `preprocess` 接管 `begin_task`（原在 C++ `poll_task`）。
- `postprocess` 真正承担"落盘 + 拆回调 + 收集 writes"职责（drain 从编排闭包移入）。
- `write_context_hash` 从 C++ 透传给 executor（需改 `set_exec_func` 签名）。
- 删除 frozen 差集（WP1）。
- `tracked_writes` 作为 executor 返回字段交给 C++（取代 C++ 自己 `end_task`）。

#### WP3-4. `src/agent/cpp/worker_agent.cpp` — `poll_task` 退化为消息层

`poll_task`（`:315-400`）从"事务编排者"退化为"消息构造与发送者"：

```cpp
bool WorkerAgent::poll_task() {
    PendingTask task = ...;   // 取 task（原 :316-322）
    if (task.task_module_ == "__fly_internal") { execute_internal_task(task); return true; }
    if (!executor_) { ...; return true; }

    // stage vars（原 :331-334）
    if (!task.var_payloads_.empty()) {
        std::lock_guard<std::mutex> lk(pending_task_vars_mutex_);
        pending_task_vars_ = std::move(task.var_payloads_);
    }

    // 调 Python executor（三阶段编排已在 Python 内完成）
    auto result = executor_->execute(task.task_id_, task.task_name_, task.task_module_, task.args_, task.write_context_hash_);

    // 从 result 取 tracked_writes（Python 已 end_task）
    auto tracked_writes = result.tracked_writes_;   // 【新增字段】
    auto frozen_dbs = current_task_frozen_dbs_;     // WP1 显式登记

    if (result.status_ == SUCCESS && WorkerAgentContext::get_last_error_type() == UNKNOWN) {
        commit_task_segments(tracked_writes);       // 成功：提交
        // 构造 TaskCompleteMessage（原 :361-376）
        TaskCompleteMessage complete;
        ... complete.frozen_dbs_ = std::move(frozen_dbs);
        reactor_->send(master_conn_, complete);
    } else {
        cleanup_failed_task_writes(tracked_writes); // 失败：回滚
        TaskFailedMessage failed;
        ... failed.pending_frozen_dbs_ = std::move(frozen_dbs);  // WP1-5 选项 a
        reactor_->send(master_conn_, failed);
    }
    outstanding_tasks_--;
    return true;
}
```

**`begin_task` / `end_task` 调用从 `poll_task` 移除**（移到 Python preprocess/postprocess）。`commit_task_segments` / `cleanup_failed_task_writes` 仍在 `poll_task` 调用（C++ 负责发消息，这两个是发消息前的最后一步）。

#### WP3-5. `TaskExecResult`（`task_executor.h:15`）新增字段

```cpp
CMVector<CMString> tracked_writes_;   // 【新增】Python postprocess 产出，供 C++ commit/cleanup
```
`agent_export.cpp:37-49` 的 dict→struct 映射新增 `tracked_writes` 键的转换。

#### WP3-6. `set_exec_func` 签名（`agent_export.cpp:30`）增加 `write_context_hash`

Python executor 闭包需要 `write_context_hash` 来调 `begin_task`。`set_exec_func` 包装的 lambda 增加 `const CMString& write_context_hash` 参数，C++ `TaskExecutor::execute` 签名对应增加。

---

## 3. 已决问题（grill-me 全部确认）

| # | 问题 | 最终决策 | 关键理由 |
|---|---|---|---|
| **Q1** | task 失败时 pending frozen 的回滚信号 | **选 b：master 按 task_id 清理**。pending 结构为 `map<db_id, task_id>`，三个清理点：`on_task_complete`(迁移→confirmed)、`on_task_failed`(按 task_id 清)、`on_disconnect`(崩溃恢复按 task_id 清)。**外加方案 P：freeze 加 ack 通道**——`DatabaseFreezeAckMessage` + 新错误类型 `DB_ALREADY_FROZEN`，worker 同步等 ack，冲突时联动 task 失败 | **崩溃失败**（worker SIGSEGV/断网，收不到失败消息）是决定性因素：靠 failed 消息携带 pending 列表（选项 a）无法覆盖崩溃场景，pending 会永久残留 → 该 db 被永久标"冻结中" → 后续所有写被拒 → **死锁级 bug**。按 task_id 清理 + `on_disconnect` 的 `get_task_ids_by_worker` 反查才能覆盖崩溃。冲突场景（多 task freeze 同 db，业务流程错误）直接 fail-fast，不引入复杂可重入逻辑 |
| **Q2** | master 自己 freeze / 写数据是否支持 pending 语义 | **否**：master 自写（`on_master_register_write`，worker_id_==0）强制走即时登记，`do_write_register` 用 `msg.worker_id_==0 \|\| streaming_mode` 判定；`on_master_freeze` 保持即时确认 + 广播，不走 pending | master 进程不设 transaction_mode、无 TaskCompleteMessage，没有延迟登记的触发时机，必须即时生效。master 进程不参与 worker 的非 stream task 三阶段 |
| **Q3** | `execute_internal_task`（backup 等）是否同步改造 | **否**：保持现状 | internal task 不经 Python executor、不涉及 freeze 差集、`frozen_dbs_` 恒为空；`is_internal_=true` 的 complete 走 master 单独分支（直接 update_remote_idx），不进依赖图、不涉及 mark_data_ready 延迟 |
| **Q4** | 删除差集后 `_db_cache` 是否仍需跨 task 保留 | **保留**：跨 task 复用 Database 才是核心功能 | `_deserialize_args`（`executor.py:52`）靠它复用 Database 避免重复创建；差集只是它的次要用途，删除不影响核心用途 |
| **Q5** | WP3 异常路径下 end_task 执行保证如何实现 | **方案 i：Python try/except 跟踪**（`began` 标志） | 状态生命周期与 try/except 块天然对应，纯 Python 无跨语言状态同步；preprocess 成功后 `began=True`，except 分支检查 `began` 补调 end_task |

---

## 4. 风险与回归测试重点

### 高风险点
1. **stream 模式行为不变性**：WP1/WP2 的所有分支必须保证 `streaming_mode==true` 时走原路径。默认配置（mode==0）是 stream，**QA 全量回归必须 100% 通过**（AGENTS.md 强制要求）。
2. **异常路径下 end_task 的执行保证**：移到 Python 后，必须确保 execute 抛异常时 end_task 仍被调用（拆回调、防泄漏）。WP3-3 的 try/except 已处理，但需单测覆盖"execute 抛异常 → end_task 仍执行 → cleanup 回滚"。
3. **跨语言调用面扩大**：WP3 新增多处 Python→C++ 调用（begin_task/end_task/commit/cleanup），需关注 GIL 与 reactor 线程的交互（参考 `docs/issues/004-flystream-gil-deadlock-analysis.md` 的教训）。
4. **freeze 回滚的完整性**：非 stream 模式 task 失败时，`pending_frozen_dbs_` 清除 + worker 本地 db 未广播（其他 worker 不感知）= 一致性保持。但 worker 本地 db 已 freeze（marker + is_frozen_），需确认 task 失败后 worker 本地是否要 reset——**当前设计：worker 本地 db 在 task 失败时也回滚 reset，因为该 db 在本 worker 上是 task 私有上下文**。

### 必须新增/覆盖的测试
| 测试场景 | 验证点 |
|---|---|
| 非 stream 模式 task 成功 + freeze | pending 按 task_id 迁移到 confirmed + 广播一次（非两次） |
| 非 stream 模式 task 失败 + 已 freeze | `rollback_pending_frozen(task_id)` 清 pending + worker 本地 db reset + 无广播 |
| **非 stream 模式 worker 崩溃 + 已 freeze**（Q1 关键） | `on_disconnect` 拿 `get_task_ids_by_worker` → `rollback_pending_frozen` 清 pending，无残留死锁 |
| 非 stream 模式跨 task frozen 检查 | task A freeze db_X（pending）→ task B write db_X 被拒（`is_db_frozen` 覆盖 pending） |
| **多 task freeze 同 db（冲突 fail-fast）**（Q1 方案 P） | task B freeze db_X → ack 返回 `DB_ALREADY_FROZEN` → worker 设 last_error_type → task B 失败 + cleanup |
| 非 stream 模式 write register 不即时可见 | write 后 mark_data_ready 未触发，complete 后才触发 |
| stream 模式全量回归 | 行为与改造前完全一致（freeze 即时广播、write 即时可见、差集已删但显式登记等价） |
| **freeze ack 超时** | master 无响应 5s → `WRITE_REGISTRATION_TIMEOUT` → task 失败 |
| postprocess drain 落盘 | execute 写入 → postprocess drain → commit 可读到完整 writes |
| 异常路径 end_task（Q5 方案 i） | execute 抛异常 → `began==True` → 补调 end_task → cleanup 回滚 → TaskFailed 发出 |
| 三阶段编排上移 | begin/end/commit/cleanup 调用顺序正确，无遗漏/重复 |
| **master 自写不受 mode 影响**（Q2） | 非 stream 模式下 master（worker_id_==0）write 仍即时 mark_data_ready |

---

## 5. 实施顺序建议

```
WP1 (freeze 延迟可见 + ack 通道 + 崩溃恢复)
  ├─ WP1-1/2: master pending frozen 状态机（map<db_id,task_id>）+ is_db_frozen 并集
  │            + commit/rollback_pending_frozen + on_database_freeze_request 分流+冲突检测
  ├─ WP1-3:   worker freeze 改同步 ack（DatabaseFreezeAckMessage + pending+cv）
  │            + DatabaseFreezeNotification 带 task_id_ + current_task_frozen_dbs_ 显式登记
  ├─ WP1-4:   executor.py 删差集
  ├─ WP1-5:   message 字段（DatabaseFreezeNotification.task_id_ / DatabaseFreezeAckMessage / DB_ALREADY_FROZEN）
  └─ 崩溃恢复: on_disconnect 新增 rollback_pending_frozen 循环
       ↓ （先跑 stream 模式回归确保未破坏，再验非 stream 冲突/崩溃场景）
WP2 (write register 可见性延迟)
  └─ WP2-1: do_write_register 拆分校验/登记段（master 自写 worker_id_==0 强制即时）
       ↓ （非 stream 模式单测）
WP3 (postprocess 重构 + 编排上移)
  ├─ WP3-1/2: 导出原语 + getter
  ├─ WP3-3:   executor.py 三阶段重构（Q5 方案 i：try/except 跟踪 began）
  ├─ WP3-4:   poll_task 退化
  └─ WP3-5/6: TaskExecResult + set_exec_func 签名
       ↓ （全量 QA 回归）
```

每个 WP 独立可验证、可提交。WP1 优先（解决最严重的语义错乱 + 死锁风险），WP3 最后（改动面最大但与 WP1/WP2 解耦）。

---

## 6. 不在本次范围

- **`_deserialize_args` 的副作用拆分**（`executor.py:46-77` 反序列化里藏了 db 创建+注册）：本次保持现状（用户明确要求）。
- **freeze 的 `on_flush`/`cleanup_temp_entries`/`flush_vars_to_disk` 副作用在 task 失败时的残留**：当前设计接受残留（主要清理临时数据，无害）。
- **worker 本地 db 在 task 失败后的 reset 跨 task 影响**：`_db_cache` 复用的 db 若被 reset，后续 task 再用该 db 时的状态——需在实施时验证，但不在设计层面阻塞。
