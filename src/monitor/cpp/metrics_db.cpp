#include <monitor/cpp/metrics_db.h>

#include <log/cpp/logger.h>

#include <sqlite3.h>

#include <atomic>
#include <memory>
#include <utility>

namespace fly {

namespace {

// SQL 错误日志节流：DB 异常（磁盘满/损坏等）不应刷屏掩盖现场，仅前若干条
// 打 ERR。计数器单调递增，无锁。
std::atomic<size_t> g_sql_err_logged{0};
constexpr size_t kMaxSqlErrLogs = 10;

void log_sql_error(const char* where, const CMString& path, sqlite3* db) {
    size_t n = g_sql_err_logged.fetch_add(1) + 1;
    if (n <= kMaxSqlErrLogs) {
        ERR("MetricsDb[{}] {}: {} ({})", path, where, db ? sqlite3_errmsg(db) : "null db", n);
    }
}

// prepare → bind → step(期望 DONE) → finalize 的一次性执行封装。
// binder 负责 sqlite3_bind_*；失败仅记日志（监控数据丢失可容忍，绝不影响主流程）。
void exec_stmt(sqlite3* db, const char* sql, const CMString& path,
               const std::function<void(sqlite3_stmt*)>& binder) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        log_sql_error("prepare", path, db);
        return;
    }
    binder(stmt);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        log_sql_error("step", path, db);
    }
    sqlite3_finalize(stmt);
}

void bind_text(sqlite3_stmt* stmt, int idx, const CMString& s) {
    sqlite3_bind_text(stmt, idx, s.c_str(), static_cast<int>(s.size()), SQLITE_TRANSIENT);
}

// NFS/落盘关键 PRAGMA 与 schema。journal_mode 必须 PERSIST（WAL 在 NFS 上
// 不可用；DELETE 每事务建删 journal 有 NFS 元数据抖动）。
constexpr const char* kSchemaSql = R"SQL(
CREATE TABLE IF NOT EXISTS meta(
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL DEFAULT '');
CREATE TABLE IF NOT EXISTS workers(
    worker_id INTEGER PRIMARY KEY,
    hostname TEXT NOT NULL DEFAULT '',
    ip TEXT NOT NULL DEFAULT '',
    role TEXT NOT NULL DEFAULT '',
    attributes TEXT NOT NULL DEFAULT '',
    first_seen_ms INTEGER NOT NULL DEFAULT 0,
    last_event_ms INTEGER NOT NULL DEFAULT 0,
    last_event TEXT NOT NULL DEFAULT '');
CREATE TABLE IF NOT EXISTS worker_samples(
    worker_id INTEGER NOT NULL,
    epoch_ms INTEGER NOT NULL,
    proc_rss_bytes INTEGER NOT NULL DEFAULT 0,
    proc_cpu_bps INTEGER NOT NULL DEFAULT 0,
    host_cpu_bps INTEGER NOT NULL DEFAULT 0,
    host_mem_total_bytes INTEGER NOT NULL DEFAULT 0,
    host_mem_avail_bytes INTEGER NOT NULL DEFAULT 0,
    host_load1_x100 INTEGER NOT NULL DEFAULT 0,
    net_read_bytes INTEGER NOT NULL DEFAULT 0,
    net_write_bytes INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY(worker_id, epoch_ms)) WITHOUT ROWID;
CREATE TABLE IF NOT EXISTS tasks(
    task_id INTEGER PRIMARY KEY,
    name TEXT NOT NULL DEFAULT '',
    module TEXT NOT NULL DEFAULT '',
    is_internal INTEGER NOT NULL DEFAULT 0,
    status TEXT NOT NULL DEFAULT '',
    worker_id INTEGER NOT NULL DEFAULT 0,
    priority INTEGER NOT NULL DEFAULT 0,
    error TEXT NOT NULL DEFAULT '',
    created_ms INTEGER NOT NULL DEFAULT 0,
    ready_ms INTEGER NOT NULL DEFAULT 0,
    started_ms INTEGER NOT NULL DEFAULT 0,
    completed_ms INTEGER NOT NULL DEFAULT 0,
    exec_start_ms INTEGER NOT NULL DEFAULT 0,
    exec_end_ms INTEGER NOT NULL DEFAULT 0,
    cpu_time_ms INTEGER NOT NULL DEFAULT 0,
    read_time_ms INTEGER NOT NULL DEFAULT 0,
    write_time_ms INTEGER NOT NULL DEFAULT 0,
    read_bytes INTEGER NOT NULL DEFAULT 0,
    write_bytes INTEGER NOT NULL DEFAULT 0,
    mem_baseline_bytes INTEGER NOT NULL DEFAULT 0,
    mem_avg_bytes INTEGER NOT NULL DEFAULT 0,
    mem_peak_bytes INTEGER NOT NULL DEFAULT 0,
    dbs TEXT NOT NULL DEFAULT '');
CREATE INDEX IF NOT EXISTS idx_tasks_worker ON tasks(worker_id);
CREATE INDEX IF NOT EXISTS idx_tasks_status ON tasks(status);
CREATE TABLE IF NOT EXISTS object_io(
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    epoch_ms INTEGER NOT NULL DEFAULT 0,
    task_id INTEGER NOT NULL DEFAULT 0,
    worker_id INTEGER NOT NULL DEFAULT 0,
    direction TEXT NOT NULL DEFAULT '',
    object_name TEXT NOT NULL DEFAULT '',
    bytes INTEGER NOT NULL DEFAULT 0,
    duration_ms INTEGER NOT NULL DEFAULT 0);
CREATE TABLE IF NOT EXISTS events(
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    epoch_ms INTEGER NOT NULL DEFAULT 0,
    category TEXT NOT NULL DEFAULT '',
    event TEXT NOT NULL DEFAULT '',
    worker_id INTEGER NOT NULL DEFAULT 0,
    task_id INTEGER NOT NULL DEFAULT 0,
    detail TEXT NOT NULL DEFAULT '');
)SQL";

}  // namespace

MetricsDb::~MetricsDb() {
    close();
}

bool MetricsDb::open(const CMString& log_dir) {
    if (db_ != nullptr) return true;  // 幂等：已打开

    db_path_ = std::string(log_dir.c_str()) + "/monitor.db";
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(db_path_.c_str(), &db,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
        log_sql_error("open", db_path_, db);
        if (db) sqlite3_close(db);
        return false;
    }

    char* err = nullptr;
    // PRAGMA 逐条执行（sqlite3_exec 多语句也行，但 journal_mode 等需看返回值）。
    auto pragma = [&](const char* sql) -> bool {
        if (sqlite3_exec(db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
            ERR("MetricsDb[{}] pragma {} failed: {}", db_path_, sql, err ? err : "?");
            sqlite3_free(err);
            err = nullptr;  // free 后置空：失败收尾分支不得二次释放
            return false;
        }
        return true;
    };
    bool ok = pragma("PRAGMA journal_mode=PERSIST") &&
              pragma("PRAGMA synchronous=NORMAL") &&
              pragma("PRAGMA busy_timeout=10000") &&
              sqlite3_exec(db, kSchemaSql, nullptr, nullptr, &err) == SQLITE_OK;
    if (!ok) {
        if (err) {
            ERR("MetricsDb[{}] schema init failed: {}", db_path_, err);
            sqlite3_free(err);
        }
        sqlite3_close(db);
        return false;
    }

    db_ = db;
    writer_running_ = true;
    writer_thread_ = std::thread(&MetricsDb::writer_loop, this);
    INFO("MetricsDb opened: {}", db_path_);
    return true;
}

void MetricsDb::close() {
    if (!writer_running_.exchange(false)) {
        // 未起过写线程（未 open 或已 close）。若仍有残留连接则直接关。
        if (db_ != nullptr) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
        return;
    }
    {
        std::lock_guard<std::mutex> lk(queue_mutex_);
        queue_cv_.notify_all();  // 持锁 notify（防 lost wakeup）
    }
    if (writer_thread_.joinable()) {
        writer_thread_.join();
    }
    // 写线程退出前已排空队列并提交，此处只需关连接（journal 已随最后一次
    // COMMIT 清理，文件即干净终态，GUI 事后只读打开安全）。
    if (db_ != nullptr) {
        sqlite3_close(db_);
        db_ = nullptr;
        INFO("MetricsDb closed: {}", db_path_);
    }
}

void MetricsDb::enqueue(WriteOp op) {
    if (db_ == nullptr) return;  // 未打开（monitor_db_enabled=0）时静默丢弃
    {
        std::lock_guard<std::mutex> lk(queue_mutex_);
        queue_.push_back(std::move(op));
        queue_cv_.notify_one();  // 持锁 notify
    }
}

size_t MetricsDb::drain_batch() {
    std::deque<WriteOp> batch;
    {
        std::lock_guard<std::mutex> lk(queue_mutex_);
        if (queue_.empty()) return 0;
        batch.swap(queue_);
    }
    // 锁外执行：单事务内跑完整批（提交风暴合并为一次 fsync）。
    sqlite3_exec(db_, "BEGIN", nullptr, nullptr, nullptr);
    for (auto& op : batch) {
        op(db_);
    }
    sqlite3_exec(db_, "COMMIT", nullptr, nullptr, nullptr);
    return batch.size();
}

void MetricsDb::writer_loop() {
    while (writer_running_.load()) {
        {
            std::unique_lock<std::mutex> lk(queue_mutex_);
            queue_cv_.wait_for(lk, std::chrono::milliseconds(500),
                               [this] { return !writer_running_.load() || !queue_.empty(); });
        }
        // 停止前排空余量（close 语义：全部落盘后再退出）。
        while (drain_batch() > 0) {}
        if (!writer_running_.load()) break;
    }
    // 最终排空：close() 置 running_=false 后仍可能有并发入队的尾巴。
    while (drain_batch() > 0) {}
}

size_t MetricsDb::pending_count_for_testing() {
    std::lock_guard<std::mutex> lk(queue_mutex_);
    return queue_.size();
}

// ---- meta ----

void MetricsDb::record_run_meta(const CMString& key, const CMString& value) {
    std::string k(key.c_str()), v(value.c_str());
    enqueue([this, k, v](sqlite3* db) {
        exec_stmt(db,
                  "INSERT INTO meta(key,value) VALUES(?1,?2) "
                  "ON CONFLICT(key) DO UPDATE SET value=excluded.value",
                  db_path_, [&](sqlite3_stmt* s) {
                      sqlite3_bind_text(s, 1, k.c_str(), -1, SQLITE_TRANSIENT);
                      sqlite3_bind_text(s, 2, v.c_str(), -1, SQLITE_TRANSIENT);
                  });
    });
}

// ---- workers ----

void MetricsDb::record_worker_registered(uint64_t worker_id, const CMString& hostname,
                                         const CMString& ip, const CMString& role,
                                         const CMString& attributes) {
    const uint64_t now = monitor_epoch_ms_now();
    std::string h(hostname.c_str()), i(ip.c_str()), r(role.c_str()), a(attributes.c_str());
    enqueue([this, worker_id, now, h, i, r, a](sqlite3* db) {
        exec_stmt(db,
                  "INSERT INTO workers(worker_id,hostname,ip,role,attributes,"
                  "first_seen_ms,last_event_ms,last_event) "
                  "VALUES(?1,?2,?3,?4,?5,?6,?6,'REGISTER') "
                  "ON CONFLICT(worker_id) DO UPDATE SET hostname=excluded.hostname,"
                  "ip=excluded.ip,role=excluded.role,attributes=excluded.attributes,"
                  "last_event_ms=excluded.last_event_ms,last_event='REGISTER'",
                  db_path_, [&](sqlite3_stmt* s) {
                      sqlite3_bind_int64(s, 1, static_cast<sqlite3_int64>(worker_id));
                      sqlite3_bind_text(s, 2, h.c_str(), -1, SQLITE_TRANSIENT);
                      sqlite3_bind_text(s, 3, i.c_str(), -1, SQLITE_TRANSIENT);
                      sqlite3_bind_text(s, 4, r.c_str(), -1, SQLITE_TRANSIENT);
                      sqlite3_bind_text(s, 5, a.c_str(), -1, SQLITE_TRANSIENT);
                      sqlite3_bind_int64(s, 6, static_cast<sqlite3_int64>(now));
                  });
        exec_stmt(db,
                  "INSERT INTO events(epoch_ms,category,event,worker_id,task_id,detail) "
                  "VALUES(?1,'worker','REGISTER',?2,0,?3)",
                  db_path_, [&](sqlite3_stmt* s) {
                      sqlite3_bind_int64(s, 1, static_cast<sqlite3_int64>(now));
                      sqlite3_bind_int64(s, 2, static_cast<sqlite3_int64>(worker_id));
                      sqlite3_bind_text(s, 3, h.c_str(), -1, SQLITE_TRANSIENT);
                  });
    });
}

void MetricsDb::record_worker_event(uint64_t worker_id, const CMString& event,
                                    const CMString& detail) {
    const uint64_t now = monitor_epoch_ms_now();
    std::string e(event.c_str()), d(detail.c_str());
    enqueue([this, worker_id, now, e, d](sqlite3* db) {
        exec_stmt(db,
                  "UPDATE workers SET last_event=?2,last_event_ms=?3 WHERE worker_id=?1",
                  db_path_, [&](sqlite3_stmt* s) {
                      sqlite3_bind_int64(s, 1, static_cast<sqlite3_int64>(worker_id));
                      sqlite3_bind_text(s, 2, e.c_str(), -1, SQLITE_TRANSIENT);
                      sqlite3_bind_int64(s, 3, static_cast<sqlite3_int64>(now));
                  });
        exec_stmt(db,
                  "INSERT INTO events(epoch_ms,category,event,worker_id,task_id,detail) "
                  "VALUES(?1,'worker',?2,?3,0,?4)",
                  db_path_, [&](sqlite3_stmt* s) {
                      sqlite3_bind_int64(s, 1, static_cast<sqlite3_int64>(now));
                      sqlite3_bind_text(s, 2, e.c_str(), -1, SQLITE_TRANSIENT);
                      sqlite3_bind_int64(s, 3, static_cast<sqlite3_int64>(worker_id));
                      sqlite3_bind_text(s, 4, d.c_str(), -1, SQLITE_TRANSIENT);
                  });
    });
}

// ---- samples ----

void MetricsDb::record_worker_samples(uint64_t worker_id, const CMVector<MonitorSample>& samples) {
    if (samples.empty()) return;
    CMVector<MonitorSample> copy(samples);  // 入队快照（调用方消息生命周期短）
    enqueue([this, worker_id, copy = std::move(copy)](sqlite3* db) {
        for (const auto& sp : copy) {
            exec_stmt(db,
                      "INSERT OR IGNORE INTO worker_samples(worker_id,epoch_ms,"
                      "proc_rss_bytes,proc_cpu_bps,host_cpu_bps,host_mem_total_bytes,"
                      "host_mem_avail_bytes,host_load1_x100,net_read_bytes,net_write_bytes) "
                      "VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10)",
                      db_path_, [&](sqlite3_stmt* s) {
                          sqlite3_bind_int64(s, 1, static_cast<sqlite3_int64>(worker_id));
                          sqlite3_bind_int64(s, 2, static_cast<sqlite3_int64>(sp.epoch_ms_));
                          sqlite3_bind_int64(s, 3, static_cast<sqlite3_int64>(sp.proc_rss_bytes_));
                          sqlite3_bind_int64(s, 4, sp.proc_cpu_bps_);
                          sqlite3_bind_int64(s, 5, sp.host_cpu_bps_);
                          sqlite3_bind_int64(s, 6, static_cast<sqlite3_int64>(sp.host_mem_total_bytes_));
                          sqlite3_bind_int64(s, 7, static_cast<sqlite3_int64>(sp.host_mem_avail_bytes_));
                          sqlite3_bind_int64(s, 8, sp.host_load1_x100_);
                          sqlite3_bind_int64(s, 9, static_cast<sqlite3_int64>(sp.net_read_bytes_));
                          sqlite3_bind_int64(s, 10, static_cast<sqlite3_int64>(sp.net_write_bytes_));
                      });
        }
    });
}

// ---- tasks ----

void MetricsDb::record_task(const TaskRow& row) {
    // 全量快照拷贝入队（调用方 TaskMetadata 是 master 内存活跃对象）。
    auto snap = std::make_shared<TaskRow>(row);
    enqueue([this, snap](sqlite3* db) {
        const TaskRow& t = *snap;
        exec_stmt(db,
                  "INSERT INTO tasks(task_id,name,module,is_internal,status,worker_id,"
                  "priority,error,created_ms,ready_ms,started_ms,completed_ms,"
                  "exec_start_ms,exec_end_ms,cpu_time_ms,read_time_ms,write_time_ms,"
                  "read_bytes,write_bytes,mem_baseline_bytes,mem_avg_bytes,"
                  "mem_peak_bytes,dbs) "
                  "VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13,?14,?15,?16,"
                  "?17,?18,?19,?20,?21,?22,?23) "
                  "ON CONFLICT(task_id) DO UPDATE SET name=excluded.name,"
                  "module=excluded.module,is_internal=excluded.is_internal,"
                  "status=excluded.status,worker_id=excluded.worker_id,"
                  "priority=excluded.priority,error=excluded.error,"
                  "created_ms=excluded.created_ms,ready_ms=excluded.ready_ms,"
                  "started_ms=excluded.started_ms,completed_ms=excluded.completed_ms,"
                  "exec_start_ms=excluded.exec_start_ms,exec_end_ms=excluded.exec_end_ms,"
                  "cpu_time_ms=excluded.cpu_time_ms,read_time_ms=excluded.read_time_ms,"
                  "write_time_ms=excluded.write_time_ms,read_bytes=excluded.read_bytes,"
                  "write_bytes=excluded.write_bytes,"
                  "mem_baseline_bytes=excluded.mem_baseline_bytes,"
                  "mem_avg_bytes=excluded.mem_avg_bytes,"
                  "mem_peak_bytes=excluded.mem_peak_bytes,dbs=excluded.dbs",
                  db_path_, [&](sqlite3_stmt* s) {
                      sqlite3_bind_int64(s, 1, static_cast<sqlite3_int64>(t.task_id_));
                      bind_text(s, 2, t.name_);
                      bind_text(s, 3, t.module_);
                      sqlite3_bind_int(s, 4, t.is_internal_ ? 1 : 0);
                      bind_text(s, 5, t.status_);
                      sqlite3_bind_int64(s, 6, static_cast<sqlite3_int64>(t.worker_id_));
                      sqlite3_bind_int(s, 7, t.priority_);
                      bind_text(s, 8, t.error_);
                      sqlite3_bind_int64(s, 9, static_cast<sqlite3_int64>(t.created_ms_));
                      sqlite3_bind_int64(s, 10, static_cast<sqlite3_int64>(t.ready_ms_));
                      sqlite3_bind_int64(s, 11, static_cast<sqlite3_int64>(t.started_ms_));
                      sqlite3_bind_int64(s, 12, static_cast<sqlite3_int64>(t.completed_ms_));
                      sqlite3_bind_int64(s, 13, static_cast<sqlite3_int64>(t.exec_start_ms_));
                      sqlite3_bind_int64(s, 14, static_cast<sqlite3_int64>(t.exec_end_ms_));
                      sqlite3_bind_int64(s, 15, static_cast<sqlite3_int64>(t.cpu_time_ms_));
                      sqlite3_bind_int64(s, 16, static_cast<sqlite3_int64>(t.read_time_ms_));
                      sqlite3_bind_int64(s, 17, static_cast<sqlite3_int64>(t.write_time_ms_));
                      sqlite3_bind_int64(s, 18, static_cast<sqlite3_int64>(t.read_bytes_));
                      sqlite3_bind_int64(s, 19, static_cast<sqlite3_int64>(t.write_bytes_));
                      sqlite3_bind_int64(s, 20, static_cast<sqlite3_int64>(t.mem_baseline_bytes_));
                      sqlite3_bind_int64(s, 21, static_cast<sqlite3_int64>(t.mem_avg_bytes_));
                      sqlite3_bind_int64(s, 22, static_cast<sqlite3_int64>(t.mem_peak_bytes_));
                      bind_text(s, 23, t.dbs_);
                  });
    });
}

void MetricsDb::record_task_event(uint64_t task_id, uint64_t worker_id, const CMString& event,
                                  const CMString& detail) {
    const uint64_t now = monitor_epoch_ms_now();
    std::string e(event.c_str()), d(detail.c_str());
    enqueue([this, task_id, worker_id, now, e, d](sqlite3* db) {
        exec_stmt(db,
                  "INSERT INTO events(epoch_ms,category,event,worker_id,task_id,detail) "
                  "VALUES(?1,'task',?2,?3,?4,?5)",
                  db_path_, [&](sqlite3_stmt* s) {
                      sqlite3_bind_int64(s, 1, static_cast<sqlite3_int64>(now));
                      sqlite3_bind_text(s, 2, e.c_str(), -1, SQLITE_TRANSIENT);
                      sqlite3_bind_int64(s, 3, static_cast<sqlite3_int64>(worker_id));
                      sqlite3_bind_int64(s, 4, static_cast<sqlite3_int64>(task_id));
                      sqlite3_bind_text(s, 5, d.c_str(), -1, SQLITE_TRANSIENT);
                  });
    });
}

void MetricsDb::record_object_io(const CMVector<ObjectIoRecord>& records) {
    if (records.empty()) return;
    auto copy = std::make_shared<CMVector<ObjectIoRecord>>(records);
    enqueue([this, copy](sqlite3* db) {
        for (const auto& r : *copy) {
            exec_stmt(db,
                      "INSERT INTO object_io(epoch_ms,task_id,worker_id,direction,"
                      "object_name,bytes,duration_ms) VALUES(?1,?2,?3,?4,?5,?6,?7)",
                      db_path_, [&](sqlite3_stmt* s) {
                          sqlite3_bind_int64(s, 1, static_cast<sqlite3_int64>(r.epoch_ms_));
                          sqlite3_bind_int64(s, 2, static_cast<sqlite3_int64>(r.task_id_));
                          sqlite3_bind_int64(s, 3, static_cast<sqlite3_int64>(r.worker_id_));
                          bind_text(s, 4, r.is_write_ ? "w" : "r");
                          bind_text(s, 5, r.object_name_);
                          sqlite3_bind_int64(s, 6, static_cast<sqlite3_int64>(r.bytes_));
                          sqlite3_bind_int64(s, 7, static_cast<sqlite3_int64>(r.duration_ms_));
                      });
        }
    });
}

// ---- 通用事件流 ----

void MetricsDb::record_event(const CMString& category, const CMString& event,
                             uint64_t worker_id, uint64_t task_id, const CMString& detail) {
    const uint64_t now = monitor_epoch_ms_now();
    std::string c(category.c_str()), e(event.c_str()), d(detail.c_str());
    enqueue([this, c, e, d, worker_id, task_id, now](sqlite3* db) {
        exec_stmt(db,
                  "INSERT INTO events(epoch_ms,category,event,worker_id,task_id,detail) "
                  "VALUES(?1,?2,?3,?4,?5,?6)",
                  db_path_, [&](sqlite3_stmt* s) {
                      sqlite3_bind_int64(s, 1, static_cast<sqlite3_int64>(now));
                      sqlite3_bind_text(s, 2, c.c_str(), -1, SQLITE_TRANSIENT);
                      sqlite3_bind_text(s, 3, e.c_str(), -1, SQLITE_TRANSIENT);
                      sqlite3_bind_int64(s, 4, static_cast<sqlite3_int64>(worker_id));
                      sqlite3_bind_int64(s, 5, static_cast<sqlite3_int64>(task_id));
                      sqlite3_bind_text(s, 6, d.c_str(), -1, SQLITE_TRANSIENT);
                  });
    });
}

}  // namespace fly
