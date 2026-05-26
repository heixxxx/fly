#include <agent/cpp/worker_agent.h>
#include <log/cpp/logger.h>
#include <core/cpp/config.h>
#include <storage/cpp/data_service.h>
#include <network/cpp/data_client.h>
#include <network/cpp/metadata_client.h>
#include <thread>
#include <chrono>
#include <unistd.h>

namespace fly {

DataService& WorkerAgent::ds() {
    return data_service_ ? *data_service_ : DataService::instance();
}

void WorkerAgent::set_data_service(DataService* ds) {
    data_service_ = ds;
    ds->set_remote_read_handler([this](const CMString& name) -> std::pair<bool, ReadResult> {
        return request_remote_data(name);
    });
    ds->set_direct_read_handler([this](const CMString& host, int32_t port,
                                         const CMString& name) -> std::pair<bool, ReadResult> {
        return request_data_from_worker(host, port, name);
    });
}

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


    auto transport = create_transport("tcp");

    transport->listen("0.0.0.0", 0);
    data_server_port_ = static_cast<int32_t>(transport->get_bound_port());
    data_server_host_ = Config::instance().get_str("data_server_host");

    INFO("data server listening on port {}", data_server_port_);

    master_conn_ = transport->connect(master_host_, master_port_);

    INFO("connected, master_conn={}", master_conn_);

    reactor_ = CMMakeUnique<Reactor>(std::move(transport));

    auto& dsInst = ds();
    int data_server_threads = static_cast<int>(Config::instance().get_int("data_server_threads"));
    dsInst.start_transfer_server(
        data_server_threads,
        [this](const TransferResult& result) {
            DataResponseMessage response;
            response.object_name = result.object_name;
            response.success = result.success;
            response.data = result.data;
            if (!result.success) {
                response.error_message = result.error_message;
            }
            reactor_->send(result.conn_id, response);
        });
    reactor_->set_io_pool(dsInst.get_transfer_pool());

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

    reactor_->register_handler<DataRequestMessage>(
        [this](uint64_t conn_id, const DataRequestMessage& msg) {
            on_data_request(conn_id, msg);
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

    reactor_->on_disconnect([this](uint64_t conn_id) {
        on_disconnect(conn_id);
    });

    reactor_thread_ = std::thread([this] { reactor_->run(); });
    reactor_->wait_until_running();

    RegisterMessage reg;
    reg.worker_id = worker_id_;
    reg.attributes = attributes_;
    reg.data_server_host = data_server_host_;
    reg.data_server_port = data_server_port_;

    char hostname_buf[256] = {};
    gethostname(hostname_buf, sizeof(hostname_buf));
    reg.hostname = hostname_buf;
    reg.ip_address = data_server_host_;

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
}

void WorkerAgent::stop() {
    if (!reactor_ && !running_) return;

    initiate_shutdown("stop() called");
    do_cleanup();
}

void WorkerAgent::do_cleanup() {
    if (heartbeat_thread_.joinable()) {
        heartbeat_thread_.join();
    }

    if (reactor_thread_.joinable()) {
        reactor_thread_.join();
    }
    reactor_.reset();

    databases_.clear();

    ds().stop_transfer_server();

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
                               const CMVector<CMString>& required_capabilities) {
     TaskSubmitMessage msg;
     msg.task_name = name;
    msg.task_module = module;
    msg.args = args;
    msg.inputs = inputs;
    msg.required_capabilities = required_capabilities;
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
            hb.worker_id = worker_id_;
            reactor_->send(master_conn_, hb);

            DBG("Heartbeat sent");
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

    PendingTask task;
    task.task_id = msg.task_id;
    task.task_name = msg.task_name;
    task.task_module = msg.task_module;
    task.args = msg.args;

    {
        std::lock_guard<std::mutex> lock(task_queue_mutex_);
        task_queue_.push(std::move(task));
    }
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

    INFO("Executing task: task_id={}", task.task_id);

    if (executor_) {
        begin_task(task.task_id);
        auto result = executor_->execute(
            task.task_id, task.task_name, task.task_module, task.args);
        auto tracked_writes = end_task(task.task_id);

        if (result.status == TaskExecStatus::SUCCESS) {
            TaskCompleteMessage complete;
            complete.task_id = task.task_id;
            complete.worker_id = worker_id_;
            complete.written_objects = std::move(tracked_writes);
            for (auto& out : result.outputs) {
                complete.written_objects.push_back(std::move(out));
            }
            complete.frozen_dbs = std::move(result.frozen_dbs);
            reactor_->send(master_conn_, complete);

            auto tid = task.task_id;
            auto out_count = complete.written_objects.size();
            INFO("TaskComplete sent: task_id={}, outputs={}", tid, out_count);
        } else {
            TaskFailedMessage failed;
            failed.task_id = task.task_id;
            failed.worker_id = worker_id_;
            failed.error_message = result.error;
            failed.error_type = WorkerAgentContext::get_last_error_type();
            reactor_->send(master_conn_, failed);

            ERR("TaskFailed sent: task_id={}, error={}", task.task_id, result.error);
        }
    }
    return true;
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
    heartbeat_running_ = false;
    heartbeat_cv_.notify_all();
    if (reactor_) {
        reactor_->stop();
    }
}

void WorkerAgent::on_db_path_response(const DbPathResponseMessage& msg) {
    touch_master_contact();

    std::lock_guard<std::mutex> lock(pending_db_path_mutex_);
    auto it = pending_db_paths_.find(msg.db_id);
    if (it != pending_db_paths_.end()) {
        it->second->base_path = msg.base_path;
        it->second->data_path = msg.data_path;
        it->second->success = msg.success;
        it->second->completed = true;
    }
}

void WorkerAgent::begin_task(uint64_t task_id) {
    current_task_id_ = task_id;
    current_writes_.clear();
    WorkerAgentContext::set(&record_write_trampoline, this);
    WorkerAgentContext::set_register_func([this](const CMString& db_id, const CMString& name) -> std::pair<CMString, TaskErrorType> {
        return register_write_with_master(db_id, name);
    });
    WorkerAgentContext::set_notify_removed_func(&notify_removed_trampoline, this);
    WorkerAgentContext::set_freeze_func(&freeze_trampoline, this);
    WorkerAgentContext::set_remove_request_func(&remove_request_trampoline, this);
}

void WorkerAgent::record_write(const CMString& db_id, const CMString& object_name) {
     CMString full_name = db_id + ":" + object_name;
     current_writes_.push_back(full_name);

    if (registered_ && Config::instance().get_int("dependency_update_mode") == 0) {
        DataReadyMessage msg;
        msg.worker_id = worker_id_;
        msg.object_name = full_name;
        msg.db_id = db_id;
        auto db_it = databases_.find(db_id);
        if (db_it != databases_.end()) {
            msg.writer_id = db_it->second->get_writer_id();
        }
        reactor_->send(master_conn_, msg);
    }
}

CMVector<CMString> WorkerAgent::end_task(uint64_t task_id) {
    WorkerAgentContext::clear();
    auto writes = std::move(current_writes_);
    current_writes_.clear();
    current_task_id_ = 0;
    return writes;
}

void WorkerAgent::record_write_trampoline(void* ctx, const CMString& db_id, const CMString& name) {
    static_cast<WorkerAgent*>(ctx)->record_write(db_id, name);
}

void WorkerAgent::notify_removed_trampoline(void* ctx, const CMString& db_id, const CMString& name) {
    auto* self = static_cast<WorkerAgent*>(ctx);
    CMString full_name = db_id + ":" + name;

    ObjectRemovedMessage msg;
    msg.object_name = full_name;
    msg.db_id = db_id;
    self->reactor_->send(self->master_conn_, msg);

    INFO("ObjectRemoved sent to master: {}", full_name);
}

void WorkerAgent::freeze_trampoline(void* ctx, const CMString& db_id) {
    static_cast<WorkerAgent*>(ctx)->request_database_freeze(db_id);
}

void WorkerAgent::request_database_freeze(const CMString& db_id) {
    if (!registered_) return;

    DatabaseFreezeNotification msg;
    msg.db_id = db_id;
    reactor_->send(master_conn_, msg);
    INFO("Freeze notification sent: db_id={}", db_id);
}

void WorkerAgent::on_data_request(uint64_t conn_id, const DataRequestMessage& msg) {

    ds().submit_transfer(conn_id, msg.object_name);
}

std::pair<bool, ReadResult> WorkerAgent::request_remote_data(const CMString& object_name) {
     auto location = MetadataClient::query_data_location(
        master_host_, master_port_, object_name);

    if (!location.found) {
        return {false, ReadResult{}};
    }

    INFO("MetadataClient resolved {} -> {}:{}", object_name, location.host, location.port);

    auto [success, data, error] = DataClient::request_data(
        location.host, location.port, object_name);

    if (!success) {
        return {false, ReadResult{}};
    }

    ds().update_remote_idx(object_name, location.worker_id, location.host, location.port);

    ReadResult result;
    result.data_buffer.assign(data.begin(), data.end());
    return {true, std::move(result)};
}

std::pair<bool, ReadResult> WorkerAgent::request_data_from_worker(const CMString& host, int32_t port,
                                                                  const CMString& object_name) {

    auto [success, data, error] = DataClient::request_data(host, port, object_name);

    if (!success) {
        return {false, ReadResult{}};
    }

    ReadResult result;
    result.data_buffer.assign(data.begin(), data.end());
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
    pending->db_id = db_id;
    {
        std::lock_guard<std::mutex> lock(pending_db_path_mutex_);
        pending_db_paths_[db_id] = pending;
    }

    DbPathRequestMessage req;
    req.db_id = db_id;
    reactor_->send(master_conn_, req);

    INFO("Sent DbPathRequest for db_id={}", db_id);

    for (int i = 0; i < 100; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        std::lock_guard<std::mutex> lock(pending_db_path_mutex_);
        if (pending->completed) {
            pending_db_paths_.erase(db_id);
            if (pending->success && !pending->base_path.empty()) {
                auto db = CMMakeShared<Database>(pending->base_path, pending->data_path, worker_id_, data_server_host_);
                databases_[db_id] = db;
                return true;
            }
            return false;
        }
    }

    {
        std::lock_guard<std::mutex> lock(pending_db_path_mutex_);
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
    auto pending = CMMakeShared<PendingWriteRegister>();
    pending->object_name = full_name;
    {
        std::lock_guard<std::mutex> lock(pending_write_reg_mutex_);
        pending_write_regs_[full_name] = pending;
    }

    WriteRegisterMessage msg;
    msg.worker_id = worker_id_;
    msg.object_name = full_name;
    msg.db_id = db_id;
    reactor_->send(master_conn_, msg);

    INFO("WriteRegister sent: object={}", full_name);

    for (int i = 0; i < 100; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        std::lock_guard<std::mutex> lock(pending_write_reg_mutex_);
        if (pending->completed) {
            pending_write_regs_.erase(full_name);
            if (!pending->success) {
                WorkerAgentContext::set_last_error_type(pending->error_type);
                return {pending->error_message, pending->error_type};
            }
            return {"", TaskErrorType::UNKNOWN};
        }
    }

    {
        std::lock_guard<std::mutex> lock(pending_write_reg_mutex_);
        pending_write_regs_.erase(full_name);
    }
    WorkerAgentContext::set_last_error_type(TaskErrorType::WRITE_REGISTRATION_TIMEOUT);
    return {"Write registration timeout for: " + full_name, TaskErrorType::WRITE_REGISTRATION_TIMEOUT};
}

void WorkerAgent::on_write_register_ack(uint64_t conn_id, const WriteRegisterAckMessage& msg) {
    touch_master_contact();

    std::lock_guard<std::mutex> lock(pending_write_reg_mutex_);
    auto it = pending_write_regs_.find(msg.object_name);
    if (it != pending_write_regs_.end()) {
        it->second->success = msg.success;
        it->second->error_message = msg.error_message;
        it->second->error_type = msg.error_type;
        it->second->completed = true;
    }
}

void WorkerAgent::on_object_removed(uint64_t conn_id, const ObjectRemovedMessage& msg) {
    touch_master_contact();
    INFO("ObjectRemoved received from master: {}", msg.object_name);

    ds().remove_local_index(msg.object_name);
    ds().remove_remote_index(msg.object_name);
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
        msg.worker_id = worker_id_;
        msg.added_properties = actually_added;
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
        msg.worker_id = worker_id_;
        msg.removed_properties = actually_removed;
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
         msg.db_id, msg.base_path, msg.writer_ids.size());

    IdxLoadAckMessage ack;
    ack.worker_id = worker_id_;
    ack.db_id = msg.db_id;

    int32_t loaded = 0;
    try {
        auto& dsRef = ds();
        dsRef.register_database(msg.db_id, msg.base_path, "");

        for (const auto& writer_id : msg.writer_ids) {
            CMString idx_path = msg.base_path + "/" + writer_id + ".idx";
            if (!std::filesystem::exists(idx_path)) {
                WARN("idx file not found: {}", idx_path);
                continue;
            }

            LocalIndex idx(idx_path);
            idx.load();
            auto all_entries = idx.get_all_entries();

            if (!all_entries.empty()) {
                dsRef.restore_entries(msg.db_id, all_entries);
                loaded++;
            }
        }

        ack.success = true;
        ack.loaded_count = loaded;
        INFO("IdxLoad complete: db_id={}, loaded {} idx files", msg.db_id, loaded);
    } catch (const std::exception& e) {
        ack.success = false;
        ack.error_message = e.what();
        ERR("IdxLoad failed: db_id={}, error={}", msg.db_id, e.what());
    }

    reactor_->send(conn_id, ack);
}

void WorkerAgent::on_database_freeze_notification(uint64_t conn_id, const DatabaseFreezeNotification& msg) {
    touch_master_contact();
    INFO("DatabaseFreezeNotification received: db_id={}", msg.db_id);

    auto it = databases_.find(msg.db_id);
    if (it != databases_.end()) {
        if (it->second->is_frozen()) {
            INFO("DB already frozen, ignoring broadcast: db_id={}", msg.db_id);
            return;
        }
        it->second->freeze();
        INFO("Worker local database frozen: db_id={}", msg.db_id);
    }
}

void WorkerAgent::remove_request_trampoline(void* ctx, const CMString& db_id, const CMString& object_name) {
    static_cast<WorkerAgent*>(ctx)->request_object_remove(db_id, object_name);
}

void WorkerAgent::request_object_remove(const CMString& db_id, const CMString& object_name) {
    CMString full = db_id + ":" + object_name;

    auto pending = CMMakeShared<PendingRemove>();
    {
        std::lock_guard<std::mutex> lock(pending_remove_mutex_);
        pending_removes_[full] = pending;
    }

    RemoveRequestMessage msg;
    msg.db_id = db_id;
    msg.object_name = full;
    reactor_->send(master_conn_, msg);
    INFO("RemoveRequest sent: {}", full);

    std::unique_lock<std::mutex> lock(pending->mutex);
    if (!pending->cv.wait_for(lock, std::chrono::seconds(30), [&]() { return pending->completed; })) {
        std::lock_guard<std::mutex> rm_lock(pending_remove_mutex_);
        pending_removes_.erase(full);
        ERR("Remove request timed out: {}", full);
        return;
    }

    {
        std::lock_guard<std::mutex> rm_lock(pending_remove_mutex_);
        pending_removes_.erase(full);
    }

    if (!pending->success) {
        ERR("Remove request failed: {}", full);
    }
}

void WorkerAgent::on_remove_ack(uint64_t conn_id, const RemoveAckMessage& msg) {
    touch_master_contact();
    INFO("RemoveAck received: object={}, success={}", msg.object_name, msg.success);

    CMSharedPtr<PendingRemove> pending;
    {
        std::lock_guard<std::mutex> lock(pending_remove_mutex_);
        auto it = pending_removes_.find(msg.object_name);
        if (it != pending_removes_.end()) {
            pending = it->second;
        }
    }

    if (pending) {
        std::lock_guard<std::mutex> lock(pending->mutex);
        pending->success = msg.success;
        pending->completed = true;
        pending->cv.notify_one();
    }
}

void WorkerAgent::on_remove_command(uint64_t conn_id, const RemoveCommandMessage& msg) {
    touch_master_contact();
    INFO("RemoveCommand received: object={}", msg.object_name);

    ds().remove_local_index(msg.object_name);
    ds().remove_remote_index(msg.object_name);

    auto db_it = databases_.find(msg.db_id);
    if (db_it != databases_.end()) {
        auto& db = db_it->second;
        // msg.object_name is already a full name (db_id:short_name),
        // use the short part to avoid double-prefixing in remove_index_entry
        CMString short_name = msg.object_name;
        CMString prefix = msg.db_id + ":";
        if (short_name.substr(0, prefix.size()) == prefix) {
            short_name = short_name.substr(prefix.size());
        }
        db->remove_index_entry(short_name);
        INFO("RemoveCommand: persisted REMOVE entry for {}", msg.object_name);
    }
}

}  // namespace fly