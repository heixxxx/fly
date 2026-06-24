#include <agent/cpp/worker_agent.h>
#include <log/cpp/logger.h>
#include <core/cpp/config.h>
#include <core/cpp/process_info.h>
#include <core/cpp/graceful_exit.h>
#include <storage/cpp/data_service.h>
#include <network/cpp/data_client.h>
#include <network/cpp/data_client_pool.h>
#include <network/cpp/metadata_client.h>
#include <thread>
#include <chrono>
#include <functional>
#include <unistd.h>

namespace fly {

WorkerAgent::WorkerAgent(uint64_t worker_id, const CMString& master_host, uint16_t master_port,
                          const CMVector<CMString>& attributes)
    : worker_id_(worker_id), master_host_(master_host), master_port_(master_port),
      attributes_(attributes), running_(false), registered_(false) {}

WorkerAgent::~WorkerAgent() {
    stop();
}

void WorkerAgent::start() {
    if (running_) return;

    shutdown_triggered_ = false;


    auto transport = create_connection_manager("tcp");
    transport->listen("0.0.0.0", 0);
    master_conn_ = transport->connect(master_host_, master_port_);
    if (master_conn_ == 0) {
        // Connection failure is non-fatal at the network layer; for a worker it
        // is fatal: a worker cannot run without its master. Abort start() cleanly
        // (running_ stays false, no reactor/data-server created) and let the caller
        // observe is_running()==false and exit.
        ERR("Failed to connect to master {}:{} — worker cannot start",
            master_host_, master_port_);
        return;
    }
    INFO("connected, master_conn={}", master_conn_);
    reactor_ = CMMakeUnique<Reactor>(std::move(transport));

    data_server_host_ = ProcessInfo::instance()->data_server_host();
    auto dsInst = DataService::instance();
    int data_server_threads = static_cast<int>(Config::instance()->get_int("data_server_threads"));
    dsInst->start_data_server(data_server_host_, 0, data_server_threads);
    data_server_port_ = static_cast<int32_t>(dsInst->get_data_port());
    INFO("data server listening on port {}", data_server_port_);

    dsInst->set_remote_compressed_read_handler([this](const CMString& name) -> std::tuple<bool, FlyBufferPtr, CMString, bool> {
        return request_remote_data(name);
    });
    dsInst->set_direct_compressed_read_handler(
        [this](const CMString& host, int32_t port,
               const CMString& name) -> std::tuple<bool, FlyBufferPtr, CMString, CMString> {
            uint64_t rid = static_cast<uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id())) ^
                           static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
            auto [success, data, py_name, hash, error] = data_client_pool_.request(host, port, name, worker_id_, rid);
            if (!success) {
                ERR("pooled request_compressed_data failed for {}: {}", name, error);
                return {false, nullptr, {}, {}};
            }
            return {true, data, std::move(py_name), std::move(hash)};
        });

    reactor_->register_handler<RegisterAckMessage>(
        [this](uint64_t conn, const RegisterAckMessage& msg) {
            on_register_ack(msg);
        });

    reactor_->register_handler<TaskAssignMessage>(
        [this](uint64_t conn, const TaskAssignMessage& msg) {
            on_task_assign(msg);
        });

    reactor_->register_handler<ShutdownMessage>(
        [this](uint64_t conn, const ShutdownMessage& msg) {
            on_shutdown(msg);
        });

    reactor_->register_handler<DbPathResponseMessage>(
        [this](uint64_t conn, const DbPathResponseMessage& msg) {
            on_db_path_response(msg);
        });

    reactor_->register_handler<WriteRegisterAckMessage>(
        [this](uint64_t conn_id, const WriteRegisterAckMessage& msg) {
            on_write_register_ack(conn_id, msg);
        });

    reactor_->register_handler<ObjectRemovedMessage>(
        [this](uint64_t conn_id, const ObjectRemovedMessage& msg) {
            on_object_removed(conn_id, msg);
        });

    reactor_->register_handler<IdxLoadCommandMessage>(
        [this](uint64_t conn_id, const IdxLoadCommandMessage& msg) {
            on_idx_load_command(conn_id, msg);
        });

    reactor_->register_handler<DatabaseFreezeNotification>(
        [this](uint64_t conn_id, const DatabaseFreezeNotification& msg) {
            on_database_freeze_notification(conn_id, msg);
        });

    reactor_->register_handler<RemoveAckMessage>(
        [this](uint64_t conn_id, const RemoveAckMessage& msg) {
            on_remove_ack(conn_id, msg);
        });

    reactor_->register_handler<RemoveCommandMessage>(
        [this](uint64_t conn_id, const RemoveCommandMessage& msg) {
            on_remove_command(conn_id, msg);
        });

    reactor_->register_handler<HeartbeatAckMessage>(
        [this](uint64_t conn_id, const HeartbeatAckMessage& msg) {
            touch_master_contact();
            DBG("HeartbeatAck received from master");
        });

    reactor_->register_handler<VarAckMessage>(
        [this](uint64_t conn_id, const VarAckMessage& msg) {
            on_var_ack(conn_id, msg);
        });

    reactor_->register_handler<VarBroadcastMessage>(
        [this](uint64_t conn_id, const VarBroadcastMessage& msg) {
            on_var_broadcast(conn_id, msg);
        });

    reactor_->on_disconnect([this](uint64_t conn_id) {
        on_disconnect(conn_id);
    });

    reactor_thread_ = std::thread([this] { reactor_->run(); });
    reactor_->wait_until_running();

    RegisterMessage reg;
    reg.worker_id_ = worker_id_;
    reg.attributes_ = attributes_;
    reg.data_server_host_ = data_server_host_;
    reg.data_server_port_ = data_server_port_;
    reg.hostname_ = ProcessInfo::instance()->hostname();
    reg.ip_address_ = data_server_host_;

    reactor_->send(master_conn_, reg);

    auto dsp = data_server_port_;
    auto attr_count = attributes_.size();
    INFO("RegisterMessage sent with data_server_port={}, attributes={}", dsp, attr_count);

    heartbeat_running_ = true;
    heartbeat_thread_ = std::thread([this] { heartbeat_loop(); });

    {
        auto now = std::chrono::system_clock::now().time_since_epoch();
        last_master_contact_.store(
            std::chrono::duration_cast<std::chrono::seconds>(now).count());
    }

    running_ = true;

    register_shutdown_callback([this]() {
        this->stop();
        fly::Logger::shutdown();
    });
}

void WorkerAgent::stop() {
    if (!reactor_ && !running_) return;

    initiate_shutdown("stop() called");
    do_cleanup();
}

void WorkerAgent::do_cleanup() {
    data_client_pool_.stop();

    if (heartbeat_thread_.joinable()) {
        heartbeat_thread_.join();
    }

    if (reactor_thread_.joinable()) {
        reactor_thread_.join();
    }
    reactor_.reset();

    databases_.clear();

    DataService::instance()->stop_data_server();

    running_ = false;
    registered_ = false;
}

bool WorkerAgent::is_running() const {
    return running_;
}

uint64_t WorkerAgent::get_worker_id() const {
    return worker_id_;
}

void WorkerAgent::set_executor(CMSharedPtr<TaskExecutor> executor) {
    executor_ = std::move(executor);
}

bool WorkerAgent::is_registered() const {
    return registered_;
}

void WorkerAgent::submit_task(const CMString& name, const CMString& module,
                               const CMVector<CMString>& args,
                               const CMVector<CMString>& inputs,
                               const CMVector<CMString>& required_capabilities,
                               float attribute_timeout,
                               const CMString& write_context_hash,
                               const CMVector<CMString>& vars) {
    // No reactor means start() failed (e.g. master unreachable) — nothing to
    // send to. Fail soft rather than crash; caller observes no progress.
    if (!reactor_) {
        ERR("submit_task '{}' ignored: worker not started (no reactor)", name);
        return;
    }
     TaskSubmitMessage msg;
     msg.task_name_ = name;
    msg.task_module_ = module;
    msg.args_ = args;
    msg.inputs_ = inputs;
    msg.required_capabilities_ = required_capabilities;
    msg.attribute_timeout_ = attribute_timeout;
    msg.write_context_hash_ = write_context_hash;
    msg.vars_ = vars;
    reactor_->send(master_conn_, msg);
}

void WorkerAgent::heartbeat_loop() {
    while (heartbeat_running_) {
        {
            std::unique_lock<std::mutex> lock(heartbeat_mutex_);
            heartbeat_cv_.wait_for(lock, std::chrono::seconds(10),
                                    [this]{ return !heartbeat_running_.load(); });
        }

        if (!heartbeat_running_) break;

        if (registered_ && heartbeat_running_) {
            HeartbeatMessage hb;
            hb.worker_id_ = worker_id_;
            if (!reactor_->try_send(master_conn_, hb)) {
                DBG("Heartbeat skipped (send busy)");
            } else {
                DBG("Heartbeat sent");
            }
        }

        if (registered_ && running_) {
            auto now = std::chrono::system_clock::now().time_since_epoch();
            auto now_sec = std::chrono::duration_cast<std::chrono::seconds>(now).count();
            auto elapsed = now_sec - last_master_contact_.load();
            if (elapsed > MASTER_TIMEOUT_SECONDS) {
                WARN("Master timeout ({}s since last contact), shutting down", elapsed);
                initiate_shutdown("master timeout");
                break;
            }
        }
    }
}

void WorkerAgent::on_register_ack(const RegisterAckMessage& msg) {
    registered_ = true;
    touch_master_contact();

    INFO("RegisterAck received, registered");
}

void WorkerAgent::on_task_assign(const TaskAssignMessage& msg) {
    touch_master_contact();

    // Store pre-fetched dependency locations for this task.
    if (!msg.dependency_locations_.empty()) {
        std::lock_guard<std::mutex> lock(prefetched_mutex_);
        for (const auto& loc : msg.dependency_locations_) {
            prefetched_locations_[loc.object_name] = {loc.worker_id, loc.host, loc.port};
            DBG("[PREFETCH] obj={} worker_id={} host={} port={}", loc.object_name, loc.worker_id, loc.host, loc.port);
        }
    }

    PendingTask task;
    task.task_id_ = msg.task_id_;
    task.task_name_ = msg.task_name_;
    task.task_module_ = msg.task_module_;
    task.args_ = msg.args_;
    task.write_context_hash_ = msg.write_context_hash_;
    task.var_payloads_ = msg.var_payloads_;

    {
        std::lock_guard<std::mutex> lock(task_queue_mutex_);
        task_queue_.push(std::move(task));
    }
    task_queue_cv_.notify_one();
    outstanding_tasks_++;
}

bool WorkerAgent::has_pending_task() const {
    std::lock_guard<std::mutex> lock(task_queue_mutex_);
    return !task_queue_.empty();
}

bool WorkerAgent::poll_task() {
    PendingTask task;
    {
        std::lock_guard<std::mutex> lock(task_queue_mutex_);
        if (task_queue_.empty()) return false;
        task = std::move(task_queue_.front());
        task_queue_.pop();
    }

    INFO("Executing task: task_id={}", task.task_id_);

    if (task.task_module_ == "__fly_internal") {
        execute_internal_task(task);
    } else if (executor_) {
        begin_task(task.task_id_, task.write_context_hash_);
        // Stage inlined vars for the Python executor to inject before the task runs.
        if (!task.var_payloads_.empty()) {
            std::lock_guard<std::mutex> lk(pending_task_vars_mutex_);
            pending_task_vars_ = std::move(task.var_payloads_);
        }
        auto result = executor_->execute(
            task.task_id_, task.task_name_, task.task_module_, task.args_);
        auto tracked_writes = end_task(task.task_id_);

        if (result.status_ == TaskExecStatus::SUCCESS) {
            auto error_type = WorkerAgentContext::get_last_error_type();
            if (error_type != TaskErrorType::UNKNOWN) {
                // write-rejection 失败：task 逻辑成功，但某次 write 被 master 拒绝。
                // 撤销本 task 已写入的脏对象。
                cleanup_failed_task_writes(tracked_writes);

                TaskFailedMessage failed;
                failed.task_id_ = task.task_id_;
                failed.worker_id_ = worker_id_;
                failed.error_message_ = "Write registration rejected: error_type=" +
                    std::to_string(static_cast<int>(error_type));
                failed.error_type_ = error_type;
                failed.dirty_objects_ = std::move(tracked_writes);
                reactor_->send(master_conn_, failed);

                ERR("Task marked failed: task_id={}, write error_type={}",
                    task.task_id_, static_cast<int>(error_type));
            } else {
                // 成功：对所有涉及的 db 打 END（提交写入段）。
                commit_task_segments(tracked_writes);

                TaskCompleteMessage complete;
                complete.task_id_ = task.task_id_;
                complete.worker_id_ = worker_id_;
                complete.written_objects_ = std::move(tracked_writes);
                for (auto& out : result.outputs_) {
                    complete.written_objects_.push_back(std::move(out));
                }
                complete.frozen_dbs_ = std::move(result.frozen_dbs_);
                reactor_->send(master_conn_, complete);

                auto tid = task.task_id_;
                auto out_count = complete.written_objects_.size();
                INFO("TaskComplete sent: task_id={}, outputs={}", tid, out_count);
            }
        } else {
            // 异常失败：撤销本 task 已写入的脏对象。
            cleanup_failed_task_writes(tracked_writes);

            TaskFailedMessage failed;
            failed.task_id_ = task.task_id_;
            failed.worker_id_ = worker_id_;
            failed.error_message_ = result.error_;
            failed.error_type_ = WorkerAgentContext::get_last_error_type();
            failed.dirty_objects_ = std::move(tracked_writes);
            reactor_->send(master_conn_, failed);

            ERR("TaskFailed sent: task_id={}, error={}", task.task_id_, result.error_);
        }
    }

    outstanding_tasks_--;
    return true;
}

bool WorkerAgent::poll_task_blocking(int timeout_ms) {
    {
        std::unique_lock<std::mutex> lock(task_queue_mutex_);
        if (task_queue_.empty()) {
            task_queue_cv_.wait_for(lock,
                std::chrono::milliseconds(timeout_ms),
                [this] { return !task_queue_.empty() || shutdown_triggered_.load(); });
        }
        if (task_queue_.empty()) return false;
    }
    return poll_task();
}

void WorkerAgent::on_shutdown(const ShutdownMessage& msg) {

    initiate_shutdown("master shutdown message");
}

void WorkerAgent::on_disconnect(uint64_t conn_id) {
    if (conn_id == master_conn_) {
        WARN("Master connection lost, shutting down");
        initiate_shutdown("master connection lost");
    }
}

void WorkerAgent::touch_master_contact() {
    auto now = std::chrono::system_clock::now().time_since_epoch();
    last_master_contact_.store(
        std::chrono::duration_cast<std::chrono::seconds>(now).count());
}

void WorkerAgent::initiate_shutdown(const CMString& reason) {
    if (shutdown_triggered_.exchange(true)) return;

    WARN("Worker shutdown initiated: {}", reason);

    registered_ = false;
    running_ = false;
    {
        std::lock_guard<std::mutex> lock(heartbeat_mutex_);
        heartbeat_running_ = false;
    }
    heartbeat_cv_.notify_all();
    task_queue_cv_.notify_all();
    if (reactor_) {
        reactor_->stop();
    }
}

void WorkerAgent::on_db_path_response(const DbPathResponseMessage& msg) {
    touch_master_contact();

    std::lock_guard<std::mutex> lock(pending_db_path_mutex_);
    auto it = pending_db_paths_.find(msg.db_id_);
    if (it != pending_db_paths_.end()) {
        it->second->base_path_ = msg.base_path_;
        it->second->data_path_ = msg.data_path_;
        it->second->success_ = msg.success_;
        it->second->completed_ = true;
    }
    pending_db_path_cv_.notify_all();
}

void WorkerAgent::begin_task(uint64_t task_id, const CMString& write_context_hash) {
    current_task_id_ = task_id;
    current_writes_.clear();
    current_write_hash_ = write_context_hash;
    // 激活事务模式：本 task 的 write_object 会被 BEGIN/END 包裹（pending 区语义）。
    // master 直接 write_object 不激活此模式（段外隐式事务，ADD 立即生效）。
    WorkerAgentContext::set_transaction_mode(true);
    WorkerAgentContext::set_current_write_hash(write_context_hash);
    WorkerAgentContext::set_last_error_type(TaskErrorType::UNKNOWN);
    WorkerAgentContext::set_record_write_func([this](const CMString& db_id, const CMString& name) {
        record_write(db_id, name);
    });
    WorkerAgentContext::set_register_func([this](const CMString& db_id, const CMString& name) -> std::pair<CMString, TaskErrorType> {
        return register_write_with_master(db_id, name);
    });
    WorkerAgentContext::set_notify_removed_func([this](const CMString& db_id, const CMString& name) {
        CMString full_name = db_id + ":" + name;
        ObjectRemovedMessage msg;
        msg.object_name_ = full_name;
        msg.db_id_ = db_id;
        reactor_->send(master_conn_, msg);
        INFO("ObjectRemoved sent to master: {}", full_name);
    });
    WorkerAgentContext::set_freeze_func([this](const CMString& db_id) {
        request_database_freeze(db_id);
    });
    WorkerAgentContext::set_remove_request_func([this](const CMString& db_id, const CMString& object_name) {
        request_object_remove(db_id, object_name);
    });
    WorkerAgentContext::set_backup_request_func([this](const CMString& db_id, const CMString& object_name) {
        request_backup(db_id, object_name);
    });
    // Var funcs: route to master over the network (synchronous set/get).
    // Names are FULL (db_id:short_name); forwarded as-is over the wire.
    WorkerAgentContext::set_set_var_func([this](const CMString& full_var_name,
                                                FlyBufferPtr value, const CMString& type_name) -> bool {
        return set_var_sync(full_var_name, value, type_name);
    });
    WorkerAgentContext::set_get_var_func([this](const CMString& full_var_name)
        -> std::tuple<bool, FlyBufferPtr, CMString> {
        return get_var_sync(full_var_name);
    });
    WorkerAgentContext::set_remove_var_func([this](const CMString& full_var_name) {
        remove_var_async(full_var_name);
    });
}

void WorkerAgent::record_write(const CMString& db_id, const CMString& object_name) {
     CMString full_name = db_id + ":" + object_name;
     current_writes_.push_back(full_name);

    if (registered_ && Config::instance()->get_int("dependency_update_mode") == 0) {
        DataReadyMessage msg;
        msg.worker_id_ = worker_id_;
        msg.object_name_ = full_name;
        msg.db_id_ = db_id;
        auto db_it = databases_.find(db_id);
        if (db_it != databases_.end()) {
            msg.writer_id_ = db_it->second->get_writer_id();
        }
        reactor_->send(master_conn_, msg);
    }
}

CMVector<CMString> WorkerAgent::end_task(uint64_t task_id) {
    WorkerAgentContext::clear();
    WorkerAgentContext::clear_current_write_hash();
    current_write_hash_.clear();
    auto writes = std::move(current_writes_);
    current_writes_.clear();
    current_task_id_ = 0;
    return writes;
}

void WorkerAgent::commit_task_segments(const CMVector<CMString>& written_objects) {
    // task 成功：对所有涉及写入的 db 打 END，提交写入段。
    // 段未开（db 无写入）的 mark_write_end 是 no-op（DataWriter::segment_active_==false）。
    CMUnorderedSet<CMString> involved_dbs;
    for (const auto& full : written_objects) {
        CMString db_id, short_name;
        if (fly::split_full_name(full, db_id, short_name)) {
            involved_dbs.insert(db_id);
        }
    }
    for (const auto& db_id : involved_dbs) {
        auto it = databases_.find(db_id);
        if (it != databases_.end()) {
            // 先 drain 保证段内所有 ADD 已落盘，再打 END 提交。
            fly::DataService::instance()->drain_write_back();
            it->second->mark_write_end();
        }
    }
}

void WorkerAgent::cleanup_failed_task_writes(const CMVector<CMString>& dirty_objects) {
    // task 失败：按 db_id 分组，对每个 db 调 abort_task_writes 撤销写入。
    // idx 打 ABORT（整段 pending 撤销）+ data 文件 truncate 回滚 + 清运行时内存。
    CMUnorderedMap<CMString, CMVector<CMString>> by_db;
    for (const auto& full : dirty_objects) {
        CMString db_id, short_name;
        if (fly::split_full_name(full, db_id, short_name)) {
            by_db[db_id].push_back(full);
        }
    }
    for (auto& [db_id, full_names] : by_db) {
        auto it = databases_.find(db_id);
        if (it == databases_.end()) continue;
        it->second->abort_task_writes(full_names);
    }
}

void WorkerAgent::request_database_freeze(const CMString& db_id) {
    if (!registered_) return;

    DatabaseFreezeNotification msg;
    msg.db_id_ = db_id;
    reactor_->send(master_conn_, msg);
    INFO("Freeze notification sent: db_id={}", db_id);
}

std::tuple<bool, FlyBufferPtr, CMString, bool> WorkerAgent::request_remote_data(const CMString& object_name) {
    // Try pre-fetched location first (from TaskAssignMessage).
    {
        std::lock_guard<std::mutex> lock(prefetched_mutex_);
        auto it = prefetched_locations_.find(object_name);
        if (it != prefetched_locations_.end()) {
            auto [wid, host, port] = it->second;
            prefetched_locations_.erase(it);

            uint64_t request_id = static_cast<uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id())) ^
                                  static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());

            auto [success, data, py_name, hash, error] = data_client_pool_.request(
                host, port, object_name, worker_id_, request_id, 30000);

            if (success) {
                DataService::instance()->update_remote_idx(object_name, wid, host, port);
                return {true, data, std::move(py_name), false};
            }
            // Prefetch miss — fall through to master query.
        }
    }

    // Fallback: query master for location.
    auto location = metadata_client_.query_data_location(
        master_host_, master_port_, object_name);

    if (!location.found_) {
        return {false, nullptr, {}, location.can_still_produce_};
    }

    uint64_t request_id = static_cast<uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id())) ^
                          static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());

    auto [success, data, py_name, hash, error] = data_client_pool_.request(
        location.host_, location.port_, object_name, worker_id_, request_id, 30000);

    if (success) {
        DataService::instance()->update_remote_idx(object_name, location.worker_id_, location.host_, location.port_);
        return {true, data, std::move(py_name), false};
    }

    auto recheck = metadata_client_.query_data_location(
        master_host_, master_port_, object_name);
    if (!recheck.found_) {
        return {false, nullptr, {}, recheck.can_still_produce_};
    }

    return {false, nullptr, {}, recheck.can_still_produce_};
}

std::pair<bool, ReadResult> WorkerAgent::request_data_from_worker(const CMString& host, int32_t port,
                                                                   const CMString& object_name) {

    auto [success, compressed_data, py_name, hash, error] = DataClient::request_compressed_data(
        host, port, object_name, worker_id_, 0);

    if (!success) {
        ERR("request_data_from_worker failed for {}: {}", object_name, error);
        return {false, ReadResult{}};
    }

    ReadResult result;
    result.data_buffer_.assign(compressed_data->data(), compressed_data->data() + compressed_data->size());
    result.py_name_ = std::move(py_name);
    return {true, std::move(result)};
}

void WorkerAgent::register_database(const CMString& db_id, CMSharedPtr<Database> db) {
    databases_[db_id] = std::move(db);
}

bool WorkerAgent::request_db_path(const CMString& db_id) {
    auto it = databases_.find(db_id);
    if (it != databases_.end()) {
        return true;
    }
    auto pending = CMMakeShared<PendingDbPath>();
    pending->db_id_ = db_id;
    {
        std::lock_guard<std::mutex> lock(pending_db_path_mutex_);
        pending_db_paths_[db_id] = pending;
    }

    DbPathRequestMessage req;
    req.db_id_ = db_id;
    reactor_->send(master_conn_, req);

    INFO("Sent DbPathRequest for db_id={}", db_id);

    {
        std::unique_lock<std::mutex> lock(pending_db_path_mutex_);
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (true) {
            if (pending->completed_) {
                pending_db_paths_.erase(db_id);
                if (pending->success_ && !pending->base_path_.empty()) {
                    // Reuse the master-assigned db_id instead of generating a
                    // fresh random one. Without this, the worker's Database
                    // would get a different db_id than the master recorded,
                    // so object names (db_id:short_name) built here would
                    // never match the master's remote_idx lookups.
                    auto db = CMMakeShared<Database>(pending->base_path_, pending->data_path_,
                                                     worker_id_, data_server_host_, db_id);
                    databases_[db_id] = db;
                    return true;
                }
                return false;
            }
            if (pending_db_path_cv_.wait_until(lock, deadline) == std::cv_status::timeout) {
                break;
            }
        }
        pending_db_paths_.erase(db_id);
    }
    return false;
}

CMSharedPtr<Database> WorkerAgent::get_database(const CMString& db_id) const {
    auto it = databases_.find(db_id);
    if (it != databases_.end()) {
        return it->second;
    }
    return nullptr;
}

std::pair<CMString, TaskErrorType> WorkerAgent::register_write_with_master(const CMString& db_id, const CMString& object_name) {
    if (!registered_) return {"", TaskErrorType::UNKNOWN};
    CMString full_name = db_id + ":" + object_name;
    CMString ctx_hash = fly::WorkerAgentContext::get_current_write_hash();

    if (!ctx_hash.empty()) {
        auto existing_entries = DataService::instance()->find_local_entries(full_name);
        if (existing_entries.has_value() && !existing_entries.value().empty()) {
            for (const auto& entry : existing_entries.value()) {
                if (!entry.write_context_hash_.empty() && entry.write_context_hash_ != ctx_hash) {
                    CMString error_msg = "Write provenance mismatch for " + full_name +
                        ": existing hash=" + entry.write_context_hash_ +
                        " new hash=" + ctx_hash;
                    ERR("{}", error_msg);
                    WorkerAgentContext::set_last_error_type(TaskErrorType::WRITE_PROVENANCE_MISMATCH);
                    return {error_msg, TaskErrorType::WRITE_PROVENANCE_MISMATCH};
                }
            }
            bool has_matching_hash = false;
            for (const auto& entry : existing_entries.value()) {
                if (entry.write_context_hash_ == ctx_hash) {
                    has_matching_hash = true;
                    break;
                }
            }
            if (has_matching_hash) {
                INFO("Write skipped (duplicate): object={}, hash={}", full_name, ctx_hash);
                WorkerAgentContext::set_last_error_type(TaskErrorType::WRITE_DUPLICATE_SKIPPED);
                return {"", TaskErrorType::UNKNOWN};
            }
        }
    }

    auto pending = CMMakeShared<PendingWriteRegister>();
    pending->object_name_ = full_name;
    {
        std::lock_guard<std::mutex> lock(pending_write_reg_mutex_);
        pending_write_regs_[full_name] = pending;
    }

    WriteRegisterMessage msg;
    msg.worker_id_ = worker_id_;
    msg.object_name_ = full_name;
    msg.db_id_ = db_id;
    msg.write_context_hash_ = ctx_hash;
    reactor_->send(master_conn_, msg);

    INFO("WriteRegister sent: object={}", full_name);

    {
        std::unique_lock<std::mutex> lock(pending_write_reg_mutex_);
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (true) {
            if (pending->completed_) {
                pending_write_regs_.erase(full_name);
                if (!pending->success_) {
                    WorkerAgentContext::set_last_error_type(pending->error_type_);
                    return {pending->error_message_, pending->error_type_};
                }
                return {"", TaskErrorType::UNKNOWN};
            }
            if (pending_write_reg_cv_.wait_until(lock, deadline) == std::cv_status::timeout) {
                break;
            }
        }
        pending_write_regs_.erase(full_name);
    }
    WorkerAgentContext::set_last_error_type(TaskErrorType::WRITE_REGISTRATION_TIMEOUT);
    return {"Write registration timeout for: " + full_name, TaskErrorType::WRITE_REGISTRATION_TIMEOUT};
}

void WorkerAgent::on_write_register_ack(uint64_t conn_id, const WriteRegisterAckMessage& msg) {
    touch_master_contact();

    std::lock_guard<std::mutex> lock(pending_write_reg_mutex_);
    auto it = pending_write_regs_.find(msg.object_name_);
    if (it != pending_write_regs_.end()) {
        it->second->success_ = msg.success_;
        it->second->error_message_ = msg.error_message_;
        it->second->error_type_ = msg.error_type_;
        it->second->completed_ = true;
    }
    pending_write_reg_cv_.notify_all();
}

// =============================================================================
// Var service: synchronous set/get (block on master VAR_ACK), async remove.
// =============================================================================

bool WorkerAgent::set_var_sync(const CMString& full_var_name,
                               FlyBufferPtr value, const CMString& type_name) {
    if (!registered_) return false;

    auto pending = CMMakeShared<PendingVarOp>();
    pending->var_name_ = full_var_name;
    {
        std::lock_guard<std::mutex> lock(pending_var_mutex_);
        pending_var_ops_[full_var_name] = pending;
    }

    VarSetMessage msg;
    msg.var_name_ = full_var_name;  // full name on the wire
    // Network boundary copy (reactor single-segment message): the FlyBufferPtr
    // bytes are copied into the message CMString. This is the one unavoidable
    // copy at the wire boundary.
    if (value) {
        msg.value_.assign(value->data(), value->size());
    }
    msg.type_name_ = type_name;
    reactor_->send(master_conn_, msg);

    DBG("VarSet sent: var={}", full_var_name);

    bool result = false;
    {
        std::unique_lock<std::mutex> lock(pending_var_mutex_);
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (true) {
            if (pending->completed_) {
                result = pending->success_;
                break;
            }
            if (pending_var_cv_.wait_until(lock, deadline) == std::cv_status::timeout) {
                ERR("VarSet timeout: var={}", full_var_name);
                break;
            }
        }
        pending_var_ops_.erase(full_var_name);
    }
    return result;
}

std::tuple<bool, FlyBufferPtr, CMString> WorkerAgent::get_var_sync(const CMString& full_var_name) {
    if (!registered_) return {false, nullptr, ""};

    auto pending = CMMakeShared<PendingVarOp>();
    pending->var_name_ = full_var_name;
    {
        std::lock_guard<std::mutex> lock(pending_var_mutex_);
        pending_var_ops_[full_var_name] = pending;
    }

    VarGetMessage msg;
    msg.var_name_ = full_var_name;  // full name on the wire
    reactor_->send(master_conn_, msg);

    DBG("VarGet sent: var={}", full_var_name);

    std::unique_lock<std::mutex> lock(pending_var_mutex_);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (true) {
        if (pending->completed_) {
            return {pending->success_, pending->value_, pending->type_name_};
        }
        if (pending_var_cv_.wait_until(lock, deadline) == std::cv_status::timeout) {
            ERR("VarGet timeout: var={}", full_var_name);
            return {false, nullptr, ""};
        }
    }
}

void WorkerAgent::remove_var_async(const CMString& full_var_name) {
    if (!registered_) return;
    VarRemoveMessage msg;
    msg.var_name_ = full_var_name;  // full name on the wire
    reactor_->send(master_conn_, msg);
    DBG("VarRemove sent (async): var={}", full_var_name);
}

CMVector<VarPayload> WorkerAgent::take_pending_task_vars() {
    std::lock_guard<std::mutex> lk(pending_task_vars_mutex_);
    CMVector<VarPayload> result = std::move(pending_task_vars_);
    pending_task_vars_.clear();
    return result;
}

void WorkerAgent::on_var_ack(uint64_t conn_id, const VarAckMessage& msg) {
    touch_master_contact();

    CMSharedPtr<PendingVarOp> to_complete;
    {
        std::lock_guard<std::mutex> lock(pending_var_mutex_);
        auto it = pending_var_ops_.find(msg.var_name_);
        if (it != pending_var_ops_.end()) {
            to_complete = it->second;
        }
    }

    if (to_complete) {
        to_complete->success_ = msg.success_;
        to_complete->error_message_ = msg.error_message_;
        to_complete->type_name_ = msg.type_name_;
        if (msg.success_ && !msg.value_.empty()) {
            // value_ is mutable: std::move it into the FlyBuffer (zero-copy). The
            // decoded ack msg is a local destroyed after this handler returns.
            auto buf = CMMakeShared<FlyBuffer>();
            buf->take(std::move(msg.value_));
            to_complete->value_ = buf;
        }
        to_complete->completed_ = true;
        pending_var_cv_.notify_all();
    }
}

void WorkerAgent::on_var_broadcast(uint64_t conn_id, const VarBroadcastMessage& msg) {
    touch_master_contact();

    // msg.var_name_ is a FULL name; split to locate the Database and the short
    // name to drop from its local cache.
    CMString db_id, short_name;
    if (fly::split_full_name(msg.var_name_, db_id, short_name)) {
        auto it = databases_.find(db_id);
        if (it != databases_.end()) {
            it->second->drop_local_var(short_name);
        }
    }
    if (msg.is_modification_reject_) {
        ERR("Var modification rejected by master (immutable): var={}", msg.var_name_);
    }
}

void WorkerAgent::on_object_removed(uint64_t conn_id, const ObjectRemovedMessage& msg) {
    touch_master_contact();
    INFO("ObjectRemoved received from master: {}", msg.object_name_);

    DataService::instance()->remove_local_index(msg.object_name_);
    DataService::instance()->remove_remote_index(msg.object_name_);
}

void WorkerAgent::set_worker_property(const CMString& prop) {
    set_worker_property(CMVector<CMString>{prop});
}

void WorkerAgent::set_worker_property(const CMVector<CMString>& props) {
    CMVector<CMString> actually_added;
    {
        std::lock_guard<std::mutex> lock(attributes_mutex_);
        for (const auto& p : props) {
            bool exists = false;
            for (const auto& a : attributes_) {
                if (a == p) { exists = true; break; }
            }
            if (!exists) {
                attributes_.push_back(p);
                actually_added.push_back(p);
            }
        }
    }

    if (!actually_added.empty() && registered_) {
        WorkerPropertyUpdateMessage msg;
        msg.worker_id_ = worker_id_;
        msg.added_properties_ = actually_added;
        reactor_->send(master_conn_, msg);

        auto wid = worker_id_;
        auto added_count = actually_added.size();
        INFO("WorkerPropertyUpdate (set): worker_id={}, added={}", wid, added_count);
    }
}

void WorkerAgent::remove_worker_property(const CMString& prop) {
    remove_worker_property(CMVector<CMString>{prop});
}

void WorkerAgent::remove_worker_property(const CMVector<CMString>& props) {
    CMVector<CMString> actually_removed;
    {
        std::lock_guard<std::mutex> lock(attributes_mutex_);
        for (const auto& p : props) {
            auto it = std::find(attributes_.begin(), attributes_.end(), p);
            if (it != attributes_.end()) {
                attributes_.erase(it);
                actually_removed.push_back(p);
            }
        }
    }

    if (!actually_removed.empty() && registered_) {
        WorkerPropertyUpdateMessage msg;
        msg.worker_id_ = worker_id_;
        msg.removed_properties_ = actually_removed;
        reactor_->send(master_conn_, msg);

        auto wid = worker_id_;
        auto removed_count = actually_removed.size();
        INFO("WorkerPropertyUpdate (remove): worker_id={}, removed={}", wid, removed_count);
    }
}

CMVector<CMString> WorkerAgent::get_worker_properties() const {
    std::lock_guard<std::mutex> lock(attributes_mutex_);
    return attributes_;
}

void WorkerAgent::on_idx_load_command(uint64_t conn_id, const IdxLoadCommandMessage& msg) {
    touch_master_contact();
    INFO("IdxLoadCommand received: db_id={}, base_path={}, writer_ids_count={}",
         msg.db_id_, msg.base_path_, msg.writer_ids_.size());

    IdxLoadAckMessage ack;
    ack.worker_id_ = worker_id_;
    ack.db_id_ = msg.db_id_;

    int32_t loaded = 0;
    CMVector<CMString> loaded_writer_ids;
    try {
        auto dsRef = DataService::instance();
        dsRef->register_database(msg.db_id_, msg.base_path_, "");

        for (const auto& writer_id : msg.writer_ids_) {
            CMString idx_path = msg.base_path_ + "/" + writer_id + ".idx";
            if (!std::filesystem::exists(idx_path)) {
                WARN("idx file not found: {}", idx_path);
                continue;
            }

            LocalIndex idx(idx_path);
            idx.load();
            if (idx.had_unclosed_segment()) {
                WARN("Detected unclosed write segment in {} (crashed task), "
                     "its data was discarded on load", idx_path);
            }
            auto all_entries = idx.get_all_entries();

            if (!all_entries.empty()) {
                dsRef->restore_entries(msg.db_id_, all_entries);
                loaded_writer_ids.push_back(writer_id);
                loaded++;
            }
        }

        ack.success_ = true;
        ack.loaded_count_ = loaded;
        ack.loaded_writer_ids_ = loaded_writer_ids;
        INFO("IdxLoad complete: db_id={}, loaded {} idx files", msg.db_id_, loaded);
    } catch (const std::exception& e) {
        ack.success_ = false;
        ack.error_message_ = e.what();
        ERR("IdxLoad failed: db_id={}, error={}", msg.db_id_, e.what());
    }

    reactor_->send(conn_id, ack);
}

void WorkerAgent::on_database_freeze_notification(uint64_t conn_id, const DatabaseFreezeNotification& msg) {
    touch_master_contact();
    INFO("DatabaseFreezeNotification received: db_id={}", msg.db_id_);

    auto it = databases_.find(msg.db_id_);
    if (it != databases_.end()) {
        if (it->second->is_frozen()) {
            INFO("DB already frozen, ignoring broadcast: db_id={}", msg.db_id_);
            return;
        }
        it->second->freeze();
        INFO("Worker local database frozen: db_id={}", msg.db_id_);
    }
}

void WorkerAgent::request_object_remove(const CMString& db_id, const CMString& object_name) {
    CMString full = db_id + ":" + object_name;

    auto pending = CMMakeShared<PendingRemove>();
    {
        std::lock_guard<std::mutex> lock(pending_remove_mutex_);
        pending_removes_[full] = pending;
    }

    RemoveRequestMessage msg;
    msg.db_id_ = db_id;
    msg.object_name_ = full;
    reactor_->send(master_conn_, msg);
    INFO("RemoveRequest sent: {}", full);

    std::unique_lock<std::mutex> lock(pending->mutex_);
    if (!pending->cv_.wait_for(lock, std::chrono::seconds(30), [&]() { return pending->completed_; })) {
        std::lock_guard<std::mutex> rm_lock(pending_remove_mutex_);
        pending_removes_.erase(full);
        ERR("Remove request timed out: {}", full);
        return;
    }

    {
        std::lock_guard<std::mutex> rm_lock(pending_remove_mutex_);
        pending_removes_.erase(full);
    }

    if (!pending->success_) {
        ERR("Remove request failed: {}", full);
    }
}

void WorkerAgent::on_remove_ack(uint64_t conn_id, const RemoveAckMessage& msg) {
    touch_master_contact();
    INFO("RemoveAck received: object={}, success={}", msg.object_name_, msg.success_);

    CMSharedPtr<PendingRemove> pending;
    {
        std::lock_guard<std::mutex> lock(pending_remove_mutex_);
        auto it = pending_removes_.find(msg.object_name_);
        if (it != pending_removes_.end()) {
            pending = it->second;
        }
    }

    if (pending) {
        std::lock_guard<std::mutex> lock(pending->mutex_);
        pending->success_ = msg.success_;
        pending->completed_ = true;
        pending->cv_.notify_one();
    }
}

void WorkerAgent::on_remove_command(uint64_t conn_id, const RemoveCommandMessage& msg) {
    touch_master_contact();
    INFO("RemoveCommand received: object={}", msg.object_name_);

    DataService::instance()->remove_local_index(msg.object_name_);
    DataService::instance()->remove_remote_index(msg.object_name_);

    auto db_it = databases_.find(msg.db_id_);
    if (db_it != databases_.end()) {
        auto& db = db_it->second;
        CMString short_name = msg.object_name_;
        CMString prefix = msg.db_id_ + ":";
        if (short_name.substr(0, prefix.size()) == prefix) {
            short_name = short_name.substr(prefix.size());
        }
        db->remove_index_entry(short_name);
        INFO("RemoveCommand: persisted REMOVE entry for {}", msg.object_name_);
    }
}

void WorkerAgent::request_backup(const CMString& db_id, const CMString& object_name) {
    if (!registered_) return;

    CMString full_name = db_id + ":" + object_name;

    BackupRequestMessage msg;
    msg.worker_id_ = worker_id_;
    msg.object_name_ = full_name;
    msg.db_id_ = db_id;
    reactor_->send(master_conn_, msg);

    INFO("BackupRequest sent: object={}", full_name);
}

void WorkerAgent::execute_internal_task(const PendingTask& task) {
    if (task.task_name_ == "__backup_object") {
        if (task.args_.size() < 2) {
            ERR("Internal backup task: insufficient args (expected object_name, db_id)");
            TaskFailedMessage failed;
            failed.task_id_ = task.task_id_;
            failed.worker_id_ = worker_id_;
            failed.error_message_ = "Internal backup: insufficient args";
            reactor_->send(master_conn_, failed);
            return;
        }

        CMString object_name = task.args_[0];
        CMString db_id = task.args_[1];

        auto db = get_database(db_id);
        if (!db) {
            if (!request_db_path(db_id)) {
                ERR("Internal backup: failed to get db_path for db_id={}", db_id);
                TaskFailedMessage failed;
                failed.task_id_ = task.task_id_;
                failed.worker_id_ = worker_id_;
                failed.error_message_ = "Internal backup: db_path request failed";
                reactor_->send(master_conn_, failed);
                return;
            }
            db = get_database(db_id);
            if (!db) {
                ERR("Internal backup: still no database for db_id={}", db_id);
                TaskFailedMessage failed;
                failed.task_id_ = task.task_id_;
                failed.worker_id_ = worker_id_;
                failed.error_message_ = "Internal backup: no database";
                reactor_->send(master_conn_, failed);
                return;
            }
        }

        db->backup_object(object_name);
        fly::DataService::instance()->drain_write_back();

        TaskCompleteMessage complete;
        complete.task_id_ = 0;  // internal task 不需要 task_id
        complete.worker_id_ = worker_id_;
        complete.written_objects_.push_back(db_id + ":" + object_name);
        complete.is_internal_ = true;
        reactor_->send(master_conn_, complete);

        INFO("Internal backup complete: object={}, db_id={}", object_name, db_id);
    } else {
        WARN("Unknown internal task: name={}", task.task_name_);
        TaskFailedMessage failed;
        failed.task_id_ = task.task_id_;
        failed.worker_id_ = worker_id_;
        failed.error_message_ = "Unknown internal task: " + task.task_name_;
        reactor_->send(master_conn_, failed);
    }
}

}  // namespace fly