#include <task/cpp/task_manager.h>
#include <log/cpp/logger.h>
#include <algorithm>
#include <cassert>
#include <chrono>

namespace fly {

// ── Helpers ────────────────────────────────────────────────────────

static int si(TaskStatus s) { return static_cast<int>(s); }

static uint64_t now_ms() {
    auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

// ── Internal ───────────────────────────────────────────────────────

void TaskManager::move_task(uint64_t task_id, TaskStatus from, TaskStatus to) {
    // Called under lock. Moves task from one bucket to another.
    if (from == to) return;
    auto it = buckets_[si(from)].find(task_id);
    if (it == buckets_[si(from)].end()) return;
    auto node = buckets_[si(from)].extract(it);
    node.mapped()->status_ = to;
    buckets_[si(to)].insert(std::move(node));
    task_status_[task_id] = to;
}

void TaskManager::maybe_cleanup_completed() {
    // Called under lock. Remove oldest COMPLETED/FAILED tasks beyond kMaxCompletedTasks.
    int terminal = static_cast<int>(buckets_[si(TaskStatus::COMPLETED)].size()
                                  + buckets_[si(TaskStatus::FAILED)].size());
    if (terminal <= kMaxCompletedTasks) return;

    // Collect terminal tasks sorted by completed_at_ ascending.
    CMVector<std::pair<uint64_t, uint64_t>> to_remove;  // (task_id, completed_at)
    for (const auto& [id, meta] : buckets_[si(TaskStatus::COMPLETED)]) {
        to_remove.push_back({id, meta->completed_at_});
    }
    for (const auto& [id, meta] : buckets_[si(TaskStatus::FAILED)]) {
        to_remove.push_back({id, meta->completed_at_});
    }
    std::sort(to_remove.begin(), to_remove.end(),
              [](const auto& a, const auto& b) { return a.second < b.second; });

    int excess = terminal - kMaxCompletedTasks;
    for (int i = 0; i < excess && i < static_cast<int>(to_remove.size()); i++) {
        uint64_t id = to_remove[i].first;
        auto st_it = task_status_.find(id);
        if (st_it == task_status_.end()) continue;
        TaskStatus st = st_it->second;
        buckets_[si(st)].erase(id);
        task_status_.erase(st_it);
    }
}

// ── Mutation ───────────────────────────────────────────────────────

void TaskManager::create_task(uint64_t task_id, const TaskSubmissionSpec& spec,
                                    const CMString& config) {
    std::lock_guard<std::mutex> lock(mutex_);

    // create_task 语义为"新建"：task_id 必须唯一。重复 id 说明存在 task_id 复用或
    // 跨线程竞态（如 submit_task 与 on_task_complete 交错）——这曾导致 graph 与
    // metadata 的完成计数永久分叉（COMPLETED-MISMATCH），调度无限卡死。
    // rerun 失败 task 必须先 remove_task 再 create_task（见 restart_failed_tasks），
    // 不应依赖此处隐式覆盖。命中此 assert = 编程 bug，立即崩溃暴露现场。
    auto st_it = task_status_.find(task_id);
    if (st_it != task_status_.end()) {
        ERR("[FATAL] create_task: duplicate task_id={} (current_status={}) — "
            "rerun must call remove_task first. Aborting to expose the race.",
            task_id, static_cast<int>(st_it->second));
        assert(false && "create_task: duplicate task_id");
    }

    auto meta = CMMakeShared<TaskMetadata>();
    meta->task_id_ = task_id;
    meta->submission_ = spec;          // 整体赋值，无逐字段复制
    meta->status_ = TaskStatus::PENDING;
    meta->config_ = config;
    meta->created_at_ = now_ms();
    meta->started_at_ = 0;
    meta->completed_at_ = 0;
    meta->assigned_worker_id_ = 0;
    buckets_[si(TaskStatus::PENDING)].emplace(task_id, std::move(meta));
    task_status_[task_id] = TaskStatus::PENDING;
}

void TaskManager::update_task_status(uint64_t task_id, TaskStatus status) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto st_it = task_status_.find(task_id);
    if (st_it == task_status_.end()) return;

    TaskStatus old = st_it->second;
    if (old == status) return;

    auto it = buckets_[si(old)].find(task_id);
    if (it == buckets_[si(old)].end()) return;

    auto ms = now_ms();
    if (status == TaskStatus::RUNNING && it->second->started_at_ == 0) {
        it->second->started_at_ = ms;
    }
    if (status == TaskStatus::COMPLETED || status == TaskStatus::FAILED) {
        it->second->completed_at_ = ms;
    }

    move_task(task_id, old, status);

    if (status == TaskStatus::COMPLETED || status == TaskStatus::FAILED) {
        maybe_cleanup_completed();
    }
}

void TaskManager::set_error(uint64_t task_id, const CMString& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto st_it = task_status_.find(task_id);
    if (st_it == task_status_.end()) return;
    auto it = buckets_[si(st_it->second)].find(task_id);
    if (it != buckets_[si(st_it->second)].end()) {
        it->second->error_message_ = error;
    }
}

void TaskManager::set_assigned_worker(uint64_t task_id, uint64_t worker_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto st_it = task_status_.find(task_id);
    if (st_it == task_status_.end()) return;
    auto it = buckets_[si(st_it->second)].find(task_id);
    if (it != buckets_[si(st_it->second)].end()) {
        it->second->assigned_worker_id_ = worker_id;
    }
}

void TaskManager::set_timestamps(uint64_t task_id, uint64_t created, uint64_t started, uint64_t completed) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto st_it = task_status_.find(task_id);
    if (st_it == task_status_.end()) return;
    auto it = buckets_[si(st_it->second)].find(task_id);
    if (it != buckets_[si(st_it->second)].end()) {
        if (created != 0) it->second->created_at_ = created;
        if (started != 0) it->second->started_at_ = started;
        if (completed != 0) it->second->completed_at_ = completed;
    }
}

void TaskManager::set_write_context_hash(uint64_t task_id, const CMString& hash) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto st_it = task_status_.find(task_id);
    if (st_it == task_status_.end()) return;
    auto it = buckets_[si(st_it->second)].find(task_id);
    if (it != buckets_[si(st_it->second)].end()) {
        it->second->submission_.write_context_hash_ = hash;
    }
}

// ── Atomic compound operations ─────────────────────────────────────

void TaskManager::fail_task(uint64_t task_id, const CMString& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto st_it = task_status_.find(task_id);
    if (st_it == task_status_.end()) return;
    TaskStatus old = st_it->second;
    if (old == TaskStatus::FAILED) {
        auto it = buckets_[si(old)].find(task_id);
        if (it != buckets_[si(old)].end()) it->second->error_message_ = error;
        return;
    }
    auto it = buckets_[si(old)].find(task_id);
    if (it == buckets_[si(old)].end()) return;
    it->second->error_message_ = error;
    it->second->completed_at_ = now_ms();
    move_task(task_id, old, TaskStatus::FAILED);
    maybe_cleanup_completed();
}

void TaskManager::assign_task(uint64_t task_id, uint64_t worker_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto st_it = task_status_.find(task_id);
    if (st_it == task_status_.end()) return;
    TaskStatus old = st_it->second;
    auto it = buckets_[si(old)].find(task_id);
    if (it == buckets_[si(old)].end()) return;
    it->second->assigned_worker_id_ = worker_id;
    if (it->second->started_at_ == 0) it->second->started_at_ = now_ms();
    move_task(task_id, old, TaskStatus::RUNNING);
}

void TaskManager::unassign_task(uint64_t task_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto st_it = task_status_.find(task_id);
    if (st_it == task_status_.end()) return;
    TaskStatus old = st_it->second;
    auto it = buckets_[si(old)].find(task_id);
    if (it == buckets_[si(old)].end()) return;
    it->second->assigned_worker_id_ = 0;
    move_task(task_id, old, TaskStatus::PENDING);
}

// ── Query ──────────────────────────────────────────────────────────

TaskMetadataPtr TaskManager::get_task(uint64_t task_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto st_it = task_status_.find(task_id);
    if (st_it == task_status_.end()) return nullptr;
    auto it = buckets_[si(st_it->second)].find(task_id);
    if (it != buckets_[si(st_it->second)].end()) return it->second;
    return nullptr;
}

CMVector<TaskMetadataPtr> TaskManager::get_tasks_by_status(TaskStatus status) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto& bucket = buckets_[si(status)];
    CMVector<TaskMetadataPtr> result;
    result.reserve(bucket.size());
    for (const auto& [id, meta] : bucket) {
        result.push_back(meta);
    }
    return result;
}

CMVector<TaskMetadataPtr> TaskManager::get_all_tasks() const {
    std::lock_guard<std::mutex> lock(mutex_);
    CMVector<TaskMetadataPtr> result;
    result.reserve(task_status_.size());
    for (int s = 0; s < 5; s++) {
        for (const auto& [id, meta] : buckets_[s]) {
            result.push_back(meta);
        }
    }
    return result;
}

bool TaskManager::has_tasks_with_status(TaskStatus status) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !buckets_[si(status)].empty();
}

int TaskManager::count_tasks_by_status(TaskStatus status) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(buckets_[si(status)].size());
}

CMVector<uint64_t> TaskManager::get_task_ids_by_status(TaskStatus status) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto& bucket = buckets_[si(status)];
    CMVector<uint64_t> result;
    result.reserve(bucket.size());
    for (const auto& [id, meta] : bucket) {
        result.push_back(id);
    }
    return result;
}

CMVector<uint64_t> TaskManager::get_task_ids_by_worker(uint64_t worker_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    CMVector<uint64_t> result;
    for (const auto& [id, meta] : buckets_[si(TaskStatus::RUNNING)]) {
        if (meta->assigned_worker_id_ == worker_id) {
            result.push_back(id);
        }
    }
    return result;
}

bool TaskManager::has_task(uint64_t task_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return task_status_.count(task_id) > 0;
}

void TaskManager::remove_task(uint64_t task_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto st_it = task_status_.find(task_id);
    if (st_it == task_status_.end()) return;
    buckets_[si(st_it->second)].erase(task_id);
    task_status_.erase(st_it);
}

}  // namespace fly
