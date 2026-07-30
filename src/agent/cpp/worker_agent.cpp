#include <agent/cpp/worker_agent.h>
#include <log/cpp/logger.h>
#include <message/cpp/message_macros.h>
#include <message/cpp/message_registry.h>
#include <core/cpp/config.h>
#include <core/cpp/system_info.h>
#include <sstream>
#include <core/cpp/process_info.h>
#include <core/cpp/graceful_exit.h>
#include <storage/cpp/data_service.h>
#include <network/cpp/data_client.h>
#include <network/cpp/data_client_pool.h>
#include <network/cpp/metadata_client.h>
#include <agent/cpp/data_fetch.h>
#include <network/cpp/tcp_socket.h>
#include <network/cpp/message_protocol.h>
#include <network/cpp/net_quality_monitor.h>
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

    // worker 进程：MSG 宏的 push 委托给 WorkerAgentContext（task 内绑定为发送 master）。
    // 非 task 上下文 context func 为 null → push no-op（符合需求）。
    fly::set_message_push_func([](fly::LogLevel level, const fly::CMString& domain_id, int32_t source, const fly::CMString& msg) {
        fly::WorkerAgentContext::push_message(static_cast<uint8_t>(level), domain_id, source, msg);
    });

    data_server_host_ = ProcessInfo::instance()->data_server_host();
    auto dsInst = DataService::instance();
    int data_server_threads = static_cast<int>(Config::instance()->get_int("data_server_threads"));
    dsInst->start_data_server(data_server_host_, 0, data_server_threads);
    data_server_port_ = static_cast<int32_t>(dsInst->get_data_port());
    INFO("data server listening on port {}", data_server_port_);

    dsInst->set_remote_compressed_read_handler([this](const CMString& name) -> std::tuple<bool, bool> {
        return request_remote_data(name);
    });
    dsInst->set_direct_compressed_read_handler(
        [this](const CMString& host, int32_t port,
               const CMString& name) -> std::tuple<bool, FlyBufferPtr, CMString, CMString, ReadError> {
            uint64_t rid = static_cast<uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id())) ^
                           static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
            auto [success, data, py_name, hash, error, rerr] = data_client_pool_.request(host, port, name, worker_id_, rid);
            if (!success) {
                ERR("pooled request_compressed_data failed for {}: {}", name, error);
                return {false, nullptr, {}, {}, rerr};
            }
            return {true, data, std::move(py_name), std::move(hash), ReadError::NONE};
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

    reactor_->register_handler<DatabaseFreezeAckMessage>(
        [this](uint64_t conn_id, const DatabaseFreezeAckMessage& msg) {
            on_database_freeze_ack(conn_id, msg);
        });

    reactor_->register_handler<DeleteDataMessage>(
        [this](uint64_t conn_id, const DeleteDataMessage& msg) {
            on_delete_data(conn_id, msg);
        });

    reactor_->register_handler<MergeCleanupMessage>(
        [this](uint64_t conn_id, const MergeCleanupMessage& msg) {
            on_merge_cleanup(conn_id, msg);
        });

    reactor_->register_handler<MessageCountRequestMessage>(
        [this](uint64_t conn_id, const MessageCountRequestMessage& msg) {
            on_message_count_request(conn_id, msg);
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

    if (Config::instance()->get_int("net_probe_enabled")) {
        probe_running_ = true;
        probe_thread_ = std::thread([this] { bandwidth_probe_loop(); });
        INFO("Bandwidth probe thread started (interval={}ms payload={}KB)",
             Config::instance()->get_int("net_probe_interval_ms"),
             Config::instance()->get_int("net_probe_payload_kb"));
    }

    {
        auto now = std::chrono::system_clock::now().time_since_epoch();
        last_master_contact_.store(
            std::chrono::duration_cast<std::chrono::seconds>(now).count());
    }

    running_ = true;

    // FLY::0000：打印启动基础信息（豁免配额，worker 仅本地 debug log，不发送 master）。
    // worker 不绑定 system sink → emit_system_message 只写本地 debug log。
    {
        fly::CMString info = fly::SystemInfo::format_startup_info("worker", data_server_port_);
        std::istringstream iss(info);
        fly::CMString line;
        while (std::getline(iss, line)) {
            if (!line.empty()) {
                fly::emit_system_message(fly::LogLevel::INFO, "FLY::0000", 0, line);
            }
        }
    }

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

    if (probe_thread_.joinable()) {
        probe_thread_.join();
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

// Background bandwidth probe. On each interval it walks every known data-server
// peer, sends a NET_PROBE_REQUEST, and times the full round-trip of the echoed
// payload. RTT and throughput feed NetQualityMonitor, which TIER2 consults to
// order replicas. The probe reuses the short-lived data-plane connection (same
// path as a real read), so the measurement reflects the actual read channel.
void WorkerAgent::bandwidth_probe_loop() {
    const int64_t interval_ms = Config::instance()->get_int("net_probe_interval_ms");
    const uint32_t payload_kb = static_cast<uint32_t>(
        Config::instance()->get_int("net_probe_payload_kb"));
    const int timeout_ms = static_cast<int>(
        Config::instance()->get_int("net_probe_timeout_ms"));
    const size_t payload_bytes = static_cast<size_t>(payload_kb) * 1024;

    while (probe_running_) {
        {
            std::unique_lock<std::mutex> lock(probe_mutex_);
            probe_cv_.wait_for(lock, std::chrono::milliseconds(interval_ms),
                                 [this]{ return !probe_running_.load(); });
        }
        if (!probe_running_) break;

        auto peers = DataService::instance()->get_all_workers();
        for (const auto& peer : peers) {
            if (!probe_running_) break;
            if (peer.worker_id_ == worker_id_) continue;  // skip self

            auto transport = create_tcp_transport();
            int fd = transport->create_connection(peer.host_, peer.port_);
            if (fd < 0) {
                DBG("[PROBE] connect failed: {}:{} errno={}", peer.host_, peer.port_, errno);
                continue;
            }
            transport->set_recv_timeout(fd, timeout_ms);
            transport->set_send_timeout(fd, timeout_ms);

            NetProbeRequestMessage req;
            req.payload_size_ = payload_bytes;
            req.probe_seq_ = peer.worker_id_;
            CMString encoded = MessageProtocol::encode(req);

            auto t0 = std::chrono::steady_clock::now();
            bool ok = transport->send_all(fd, encoded.data(), encoded.size());
            // Read the response frame: 5B header, then payload_len bytes.
            char hdr[5];
            for (size_t got = 0; ok && got < 5;) {
                ssize_t n = transport->recv(fd, hdr + got, 5 - got);
                if (n <= 0) { ok = false; break; }
                got += static_cast<size_t>(n);
            }
            uint32_t total_len = 0;
            if (ok) {
                total_len = read_be32(hdr);
            }
            uint32_t remain = (ok && total_len >= 1) ? total_len - 1 : 0;
            CMString rest(remain, '\0');
            for (size_t got = 0; ok && got < remain;) {
                ssize_t n = transport->recv(fd, rest.data() + got, remain - got);
                if (n <= 0) { ok = false; break; }
                got += static_cast<size_t>(n);
            }
            transport->close(fd);

            if (!ok) {
                DBG("[PROBE] exchange failed: {}:{}", peer.host_, peer.port_);
                continue;
            }
            double rtt_ms = std::chrono::duration<double, std::milli>(
                                std::chrono::steady_clock::now() - t0).count();
            double mbps = (rtt_ms > 0)
                ? (static_cast<double>(payload_bytes) * 8.0) / (rtt_ms / 1000.0) / 1e6
                : 0.0;
            NetQualityMonitor::instance().update_rtt(peer.host_, rtt_ms);
            NetQualityMonitor::instance().update_bandwidth(peer.host_, mbps);
            DBG("[PROBE] {}:{} rtt={:.2f}ms bw={:.1f}Mbps", peer.host_, peer.port_, rtt_ms, mbps);
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

    // Pre-fetched dependency locations go straight into the persistent
    // remote_idx (single source of truth) so the first read hits TIER2 instead
    // of falling through to a TIER3 master query. dependency_locations_ already
    // carries all replicas.
    if (!msg.dependency_locations_.empty()) {
        for (const auto& loc : msg.dependency_locations_) {
            DataService::instance()->update_remote_idx(loc.object_name, loc.worker_id, loc.host, loc.port);
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
                // 实际写出对象（含 size，从 current_write_sizes_ 取）。
                for (const auto& name : tracked_writes) {
                    int64_t sz = 0;
                    auto it = current_write_sizes_.find(name);
                    if (it != current_write_sizes_.end()) sz = it->second;
                    complete.written_objects_.push_back({name, sz});
                }
                // 声明性输出（task 装饰器声明，非实际 write，size=0）。
                for (auto& out : result.outputs_) {
                    complete.written_objects_.push_back({std::move(out), 0});
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
    {
        std::lock_guard<std::mutex> lock(probe_mutex_);
        probe_running_ = false;
    }
    probe_cv_.notify_all();
    task_queue_cv_.notify_all();
    if (reactor_) {
        reactor_->stop();
    }
}

void WorkerAgent::on_db_path_response(const DbPathResponseMessage& msg) {
    touch_master_contact();
    pending_db_paths_.complete(msg.db_id_, [&](PendingDbPath& p) {
        p.base_path_ = msg.base_path_;
        p.data_path_ = msg.data_path_;
        p.success_ = msg.success_;
        p.completed_ = true;
    });
}

void WorkerAgent::begin_task(uint64_t task_id, const CMString& write_context_hash) {
    current_task_id_ = task_id;
    current_writes_.clear();
    current_write_sizes_.clear();
    current_write_hash_ = write_context_hash;
    // 激活事务模式：本 task 的 write_object 会被 BEGIN/END 包裹（pending 区语义）。
    // master 直接 write_object 不激活此模式（段外隐式事务，ADD 立即生效）。
    WorkerAgentContext::set_transaction_mode(true);
    WorkerAgentContext::set_current_write_hash(write_context_hash);
    WorkerAgentContext::set_last_error_type(TaskErrorType::UNKNOWN);
    WorkerAgentContext::set_record_write_func([this](const CMString& db_id, const CMString& name, int64_t size) {
        record_write(db_id, name, size);
    });
    WorkerAgentContext::set_register_func([this](const CMString& db_id, const CMString& name, int64_t size) -> std::pair<CMString, TaskErrorType> {
        return register_write_with_master(db_id, name, size);
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
    // Message 推送：task 内 MSG 宏 / fly.message() 经 context 路由到 master。
    // level 用 uint8_t（LogLevel 的 underlying 值）传递，common 模块不依赖 log。
    WorkerAgentContext::set_push_message_func([this](uint8_t level, const CMString& domain_id, int32_t source, const CMString& msg) {
        send_message_to_master(static_cast<LogLevel>(level), domain_id, source, msg);
    });
}

void WorkerAgent::record_write(const CMString& db_id, const CMString& object_name, int64_t size) {
    CMString full_name = db_id + ":" + object_name;
    current_writes_.push_back(full_name);
    current_write_sizes_[full_name] = size;
}

CMVector<CMString> WorkerAgent::end_task(uint64_t task_id) {
    WorkerAgentContext::clear();
    WorkerAgentContext::clear_current_write_hash();
    current_write_hash_.clear();
    auto writes = std::move(current_writes_);
    current_writes_.clear();
    current_write_sizes_.clear();
    current_task_id_ = 0;
    return writes;
}

void WorkerAgent::commit_task_segments(const CMVector<CMString>& written_objects) {
    // task 成功：对所有涉及写入的 db 打 END，提交写入段。
    // 段未开（db 无写入）的 mark_write_end 是 no-op（DataWriter::segment_active_==false）。
    CMUnorderedSet<CMString> involved_dbs;
    for (const auto& full : written_objects) {
        auto [db_id, short_name] = fly::split_full_name(full);
        if (!db_id.empty()) {
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
        auto [db_id, short_name] = fly::split_full_name(full);
        if (!db_id.empty()) {
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

    // 同步等 ack（仿 register_write_with_master 的 pending+cv 模式）：
    // 非 stream 模式 master 登记 pending；冲突时回 DB_ALREADY_FROZEN 联动 task 失败。
    auto pending = CMMakeShared<PendingFreezeAck>();
    pending->db_id_ = db_id;
    pending_freezes_.emplace(db_id, pending);

    DatabaseFreezeNotification msg;
    msg.db_id_ = db_id;
    msg.task_id_ = current_task_id_;   // 非 stream 模式 master 登记 pending 需要
    reactor_->send(master_conn_, msg);
    INFO("Freeze notification sent: db_id={}, task_id={}", db_id, current_task_id_);

    auto result = pending_freezes_.wait_for(db_id, std::chrono::seconds(5),
        [](const CMSharedPtr<PendingFreezeAck>& p) { return p->completed_; });
    pending_freezes_.erase(db_id);
    if (!result) {
        // 超时（master 无响应）→ 当失败处理，联动 task 失败
        WorkerAgentContext::set_last_error_type(TaskErrorType::WRITE_REGISTRATION_TIMEOUT);
        ERR("Freeze ack timeout: db_id={}", db_id);
        return;
    }
    if (!pending->success_) {
        // 冲突（DB_ALREADY_FROZEN）→ 联动 task 失败（poll_task 检查 last_error_type）
        WorkerAgentContext::set_last_error_type(pending->error_type_);
        ERR("Freeze rejected: db_id={}, error_type={}", db_id,
            static_cast<int>(pending->error_type_));
        return;
    }
    INFO("Freeze acked: db_id={}", db_id);
}

std::tuple<bool, bool> WorkerAgent::request_remote_data(const CMString& object_name) {
    // TIER3: pure location query. Ask master for ALL replicas, refresh local
    // remote_idx, and signal read_raw_compressed to re-enter TIER2 (which owns
    // the actual reads + backoff). We do NOT fetch object data here.
    auto location = metadata_client_.query_data_location(
        master_host_, master_port_, object_name);

    if (!location.found_) {
        return {false, location.can_still_produce_};
    }

    // Populate remote_idx with every replica master returned, so TIER2 can
    // iterate all of them.
    for (const auto& rl : location.all_locations_) {
        DataService::instance()->update_remote_idx(object_name, rl.worker_id_, rl.host_, rl.port_);
    }

    return {true, location.can_still_produce_};
}

std::pair<bool, ReadResult> WorkerAgent::request_data_from_worker(const CMString& host, int32_t port,
                                                                   const CMString& object_name) {
    return fetch_from_worker(host, port, object_name, worker_id_);
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
    pending_db_paths_.emplace(db_id, pending);

    DbPathRequestMessage req;
    req.db_id_ = db_id;
    reactor_->send(master_conn_, req);

    INFO("Sent DbPathRequest for db_id={}", db_id);

    auto result = pending_db_paths_.wait_for(db_id, std::chrono::seconds(5),
        [](const CMSharedPtr<PendingDbPath>& p) { return p->completed_; });
    pending_db_paths_.erase(db_id);
    if (result && result->success_ && !result->base_path_.empty()) {
        // Reuse the master-assigned db_id instead of generating a
        // fresh random one. Without this, the worker's Database
        // would get a different db_id than the master recorded,
        // so object names (db_id:short_name) built here would
        // never match the master's remote_idx lookups.
        auto db = CMMakeShared<Database>(result->base_path_, result->data_path_,
                                         worker_id_, data_server_host_, db_id);
        databases_[db_id] = db;
        return true;
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

std::pair<CMString, TaskErrorType> WorkerAgent::register_write_with_master(const CMString& db_id, const CMString& object_name, int64_t compressed_size) {
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
    pending_write_regs_.emplace(full_name, pending);

    WriteRegisterMessage msg;
    msg.worker_id_ = worker_id_;
    msg.object_name_ = full_name;
    msg.db_id_ = db_id;
    msg.write_context_hash_ = ctx_hash;
    msg.size_bytes_ = compressed_size;
    auto db_it = databases_.find(db_id);
    if (db_it != databases_.end()) {
        msg.writer_id_ = db_it->second->get_writer_id();
    }
    reactor_->send(master_conn_, msg);

    INFO("WriteRegister sent: object={}", full_name);

    auto result = pending_write_regs_.wait_for(full_name, std::chrono::seconds(5),
        [](const CMSharedPtr<PendingWriteRegister>& p) { return p->completed_; });
    pending_write_regs_.erase(full_name);
    if (result && !result->success_) {
        WorkerAgentContext::set_last_error_type(result->error_type_);
        return {result->error_message_, result->error_type_};
    }
    if (result) {
        return {"", TaskErrorType::UNKNOWN};
    }
    WorkerAgentContext::set_last_error_type(TaskErrorType::WRITE_REGISTRATION_TIMEOUT);
    return {"Write registration timeout for: " + full_name, TaskErrorType::WRITE_REGISTRATION_TIMEOUT};
}

void WorkerAgent::on_write_register_ack(uint64_t conn_id, const WriteRegisterAckMessage& msg) {
    touch_master_contact();
    pending_write_regs_.complete(msg.object_name_, [&](PendingWriteRegister& p) {
        p.success_ = msg.success_;
        p.error_message_ = msg.error_message_;
        p.error_type_ = msg.error_type_;
        p.completed_ = true;
    });
}

// =============================================================================
// Var service: synchronous set/get (block on master VAR_ACK), async remove.
// =============================================================================

bool WorkerAgent::set_var_sync(const CMString& full_var_name,
                               FlyBufferPtr value, const CMString& type_name) {
    if (!registered_) return false;

    auto pending = CMMakeShared<PendingVarOp>();
    pending->var_name_ = full_var_name;
    pending_var_ops_.emplace(full_var_name, pending);

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

    auto result = pending_var_ops_.wait_for(full_var_name, std::chrono::seconds(5),
        [](const CMSharedPtr<PendingVarOp>& p) { return p->completed_; });
    pending_var_ops_.erase(full_var_name);
    if (!result) {
        ERR("VarSet timeout: var={}", full_var_name);
        return false;
    }
    return result->success_;
}

std::tuple<bool, FlyBufferPtr, CMString> WorkerAgent::get_var_sync(const CMString& full_var_name) {
    if (!registered_) return {false, nullptr, ""};

    auto pending = CMMakeShared<PendingVarOp>();
    pending->var_name_ = full_var_name;
    pending_var_ops_.emplace(full_var_name, pending);

    VarGetMessage msg;
    msg.var_name_ = full_var_name;  // full name on the wire
    reactor_->send(master_conn_, msg);

    DBG("VarGet sent: var={}", full_var_name);

    auto result = pending_var_ops_.wait_for(full_var_name, std::chrono::seconds(5),
        [](const CMSharedPtr<PendingVarOp>& p) { return p->completed_; });
    pending_var_ops_.erase(full_var_name);
    if (!result) {
        ERR("VarGet timeout: var={}", full_var_name);
        return {false, nullptr, ""};
    }
    return {result->success_, result->value_, result->type_name_};
}

void WorkerAgent::remove_var_async(const CMString& full_var_name) {
    if (!registered_) return;
    VarRemoveMessage msg;
    msg.var_name_ = full_var_name;  // full name on the wire
    reactor_->send(master_conn_, msg);
    DBG("VarRemove sent (async): var={}", full_var_name);
}

void WorkerAgent::send_message_to_master(LogLevel level, const CMString& domain_id, int32_t source, const CMString& msg) {
    if (!registered_ || master_conn_ == 0) return;
    LogMessage m;
    m.worker_id_ = worker_id_;
    m.level_ = level;
    m.domain_id_ = domain_id;
    m.source_ = source;
    m.msg_ = msg;
    reactor_->send(master_conn_, m);
}

void WorkerAgent::on_message_count_request(uint64_t conn_id, const MessageCountRequestMessage& /*msg*/) {
    // summary 屏障：把本地 message 触发计数（id 级 + domain 级两套）上报给 master。
    MessageCountReportMessage report;
    report.worker_id_ = worker_id_;
    auto id_counts = MessageRegistry::instance().id_counts_snapshot();
    auto dom_counts = MessageRegistry::instance().domain_counts_snapshot();
    for (const auto& [k, v] : id_counts) {
        report.id_keys_.push_back(k);
        report.id_values_.push_back(v);
    }
    for (const auto& [k, v] : dom_counts) {
        report.domain_keys_.push_back(k);
        report.domain_values_.push_back(v);
    }
    if (conn_id != 0) {
        reactor_->send(conn_id, report);
    } else if (master_conn_ != 0) {
        // 兜底：conn_id 为 0（理论不会发生）时回 master_conn_。
        reactor_->send(master_conn_, report);
    }
}

CMVector<VarPayload> WorkerAgent::take_pending_task_vars() {
    std::lock_guard<std::mutex> lk(pending_task_vars_mutex_);
    CMVector<VarPayload> result = std::move(pending_task_vars_);
    pending_task_vars_.clear();
    return result;
}

void WorkerAgent::on_var_ack(uint64_t conn_id, const VarAckMessage& msg) {
    touch_master_contact();

    // Two-phase: take the shared_ptr out under the lock, then mutate it
    // outside the lock (FlyBuffer construction must not hold the map lock).
    auto to_complete = pending_var_ops_.take_for_complete(msg.var_name_);
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
        pending_var_ops_.notify_all();
    }
}

void WorkerAgent::on_var_broadcast(uint64_t conn_id, const VarBroadcastMessage& msg) {
    touch_master_contact();

    // msg.var_name_ is a FULL name; split to locate the Database and the short
    // name to drop from its local cache.
    auto [db_id, short_name] = fly::split_full_name(msg.var_name_);
    if (!db_id.empty()) {
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

void WorkerAgent::on_database_freeze_ack(uint64_t conn_id, const DatabaseFreezeAckMessage& msg) {
    touch_master_contact();
    pending_freezes_.complete(msg.db_id_, [&](PendingFreezeAck& p) {
        p.success_ = msg.success_;
        p.error_type_ = msg.error_type_;
        p.completed_ = true;
    });
}

void WorkerAgent::request_object_remove(const CMString& db_id, const CMString& object_name) {
    CMString full = db_id + ":" + object_name;

    auto pending = CMMakeShared<PendingRemove>();
    pending_removes_.emplace(full, pending);

    RemoveRequestMessage msg;
    msg.db_id_ = db_id;
    msg.object_name_ = full;
    reactor_->send(master_conn_, msg);
    INFO("RemoveRequest sent: {}", full);

    auto result = pending_removes_.wait_for(full, std::chrono::seconds(30),
        [](const CMSharedPtr<PendingRemove>& p) { return p->completed_; });
    pending_removes_.erase(full);
    if (!result) {
        ERR("Remove request timed out: {}", full);
        return;
    }
    if (!result->success_) {
        ERR("Remove request failed: {}", full);
    }
}

void WorkerAgent::on_remove_ack(uint64_t conn_id, const RemoveAckMessage& msg) {
    touch_master_contact();
    INFO("RemoveAck received: object={}, success={}", msg.object_name_, msg.success_);
    pending_removes_.complete(msg.object_name_, [&](PendingRemove& p) {
        p.success_ = msg.success_;
        p.completed_ = true;
    });
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
        // internal backup task：size=0。时序约定：backup 副本的真实 size 已由 do_backup_write
        // 的 register 路径（同步，先于本 TaskComplete）登记给 master；此处 TaskComplete 仅通知
        // 对象位置。master on_task_complete 的 is_internal_ 分支调 update_remote_idx(..., size_bytes_=0)，
        // 因 size_bytes==0 时保持原 size 不变，不会覆盖已登记的真实 size。
        complete.written_objects_.push_back({db_id + ":" + object_name, 0});
        complete.is_internal_ = true;
        reactor_->send(master_conn_, complete);

        INFO("Internal backup complete: object={}, db_id={}", object_name, db_id);
    } else if (task.task_name_ == "__merge_object") {
        // args: [short_name, db_id, base_path, target_data_path, source_host]
        // base_path = 源 db 共享 base_path（用于 idx 落盘到共享盘，master 可直读）
        // target_data_path = master host 本地 data_path（.dat 集中目标）
        if (task.args_.size() < 4) {
            ERR("Internal merge task: insufficient args (expected short_name, db_id, base_path, target_data_path)");
            TaskFailedMessage failed;
            failed.task_id_ = task.task_id_;
            failed.worker_id_ = worker_id_;
            failed.error_message_ = "Internal merge: insufficient args";
            reactor_->send(master_conn_, failed);
            return;
        }
        CMString short_name = task.args_[0];
        CMString db_id = task.args_[1];
        CMString base_path = task.args_[2];
        CMString target_data_path = task.args_[3];

        execute_merge_object(task.task_id_, short_name, db_id, base_path, target_data_path);
    } else {
        WARN("Unknown internal task: name={}", task.task_name_);
        TaskFailedMessage failed;
        failed.task_id_ = task.task_id_;
        failed.worker_id_ = worker_id_;
        failed.error_message_ = "Unknown internal task: " + task.task_name_;
        reactor_->send(master_conn_, failed);
    }
}

DataWriter* WorkerAgent::get_or_create_merge_writer(const CMString& base_path,
                                                     const CMString& target_data_path) {
    std::lock_guard<std::mutex> lk(merge_writers_mutex_);
    auto it = merge_writers_.find(target_data_path);
    if (it != merge_writers_.end()) {
        return it->second.get();
    }
    // 每个 target_data_path 独占一个 writer_id（merge 专用，避免与源 writer_id 冲突）。
    // idx 写 base_path（共享盘，master 可直读）；.dat 写 target_data_path（master host 本地）。
    CMString merge_writer_id = generate_writer_id();
    int64_t threshold = Config::instance()->get_int("aggregation_threshold");
    auto writer = CMMakeUnique<DataWriter>(
        base_path, target_data_path, merge_writer_id, threshold, data_server_host_);
    DataWriter* raw = writer.get();
    merge_writers_[target_data_path] = std::move(writer);
    INFO("Created merge writer: target_data_path={}, writer_id={}", target_data_path, merge_writer_id);
    return raw;
}

void WorkerAgent::execute_merge_object(uint64_t task_id, const CMString& short_name, const CMString& db_id,
                                        const CMString& base_path, const CMString& target_data_path) {
    CMString full = db_id + ":" + short_name;
    INFO("Internal merge: object={}, db_id={}, target_data_path={}", short_name, db_id, target_data_path);

    auto ds = DataService::instance();

    // 1. 跨机拉源对象压缩字节。本地必 miss（merge worker 未写过该对象），自动走 TIER2/TIER3
    //    回源到持有该对象的源 host worker 的 DataServer。
    auto [found, comp_data, py_name, source_hash, can_still_produce] =
        ds->read_raw_compressed(full);
    if (!found || !comp_data || comp_data->empty()) {
        ERR("Internal merge: no data for '{}'", full);
        TaskFailedMessage failed;
        failed.task_id_ = task_id;
        failed.worker_id_ = worker_id_;
        failed.error_message_ = "Internal merge: source object unavailable: " + full;
        reactor_->send(master_conn_, failed);
        return;
    }

    // 2. 解析 ObjectHeader 拿到 total_size / chunk_count（落盘需要）。
    int64_t h_off = 0;
    ObjectHeader header = ObjectHeader::deserialize(
        CMString(comp_data->data(), comp_data->size()), h_off);

    // 3. 确保 DataService 知道这个 db 的路径（base_path 共享读 idx，target_data_path 本地 .dat）。
    //    register_database 幂等（已注册则更新）；不构造 Database 避免析构副作用。
    //    这让本 worker 的 DataServer 能服务 merge 后的对象（try_read_local_raw 查 db_paths_）。
    DataWriter* writer = get_or_create_merge_writer(base_path, target_data_path);
    ds->register_database(db_id, base_path, target_data_path, writer->writer_id());

    // 4. 落盘（零解压直写 .dat + idx）。
    ds->on_write_started(db_id, full);
    CMString merge_hash = source_hash;
    writer->write_record(full, header.total_size_, header.chunk_count_, *comp_data, merge_hash);
    writer->flush();

    // 5. 登记 local_idx_（让本 worker 的 DataServer / read_raw_compressed 能本地命中）。
    //    只登记本次 write_record 新写的 entry（get_last_entry），不登记从源 idx 加载的
    //    历史 entry（它们的 file_name_ 指向源 .dat，在本 worker 不存在）。
    auto last_entry_opt = writer->get_last_entry(full);
    if (last_entry_opt.has_value()) {
        CMVector<IndexEntry> new_entries;
        new_entries.push_back(last_entry_opt.value());
        ds->on_write_completed(db_id, full, new_entries);
        ds->on_object_flushed(full);
    }

    // 6. TaskComplete。master 的 on_task_complete internal 分支会调 update_remote_idx 登记
    //    对象位置（指向本 worker）。不调 register_write_with_master——那会被 frozen db 检查拒绝
    //    （merge 是数据迁移不是新写，db 已 freeze，master 用 on_task_complete 的 internal 路径绕过）。
    int64_t comp_size = static_cast<int64_t>(comp_data->size());
    TaskCompleteMessage complete;
    complete.task_id_ = task_id;
    complete.worker_id_ = worker_id_;
    complete.written_objects_.push_back({full, comp_size});
    complete.is_internal_ = true;
    reactor_->send(master_conn_, complete);

    INFO("Internal merge complete: object={}, db_id={}, bytes={}", short_name, db_id, comp_size);
}

void WorkerAgent::on_delete_data(uint64_t conn_id, const DeleteDataMessage& msg) {
    touch_master_contact();
    INFO("DeleteData received: db_id={}, data_path={}, writer_ids_count={}",
         msg.db_id_, msg.data_path_, msg.writer_ids_.size());

    DeleteDataAckMessage ack;
    ack.worker_id_ = worker_id_;
    ack.db_id_ = msg.db_id_;

    int32_t deleted = 0;
    CMVector<CMString> deleted_writers;
    try {
        // 直接用消息显式指定的 data_path_（源 data_path），不查 db_registry ——
        // cleanup_after_merge 会把 master 的 db_registry 更新到 merge 路径，若删源在
        // cleanup 之后执行，db_registry 解析会拿到错误的（merge 后的）路径。
        // data_path_ 空时兜底用 base_path_（向后兼容无 data_path_ 的旧调用方）。
        CMString data_dir = msg.data_path_.empty() ? msg.base_path_ : msg.data_path_;

        // merge 语义：全部数据已迁到 master host，源 data_dir 下所有 .dat 都应清理。
        // data_dir 是该 db 的 data_path（一个 db 一个 data_dir），删全部 .dat 是安全的。
        // 不依赖 idx（idx 可能跨进程 writer_id 混淆，file_name_ 不一定匹配磁盘文件）。
        if (std::filesystem::exists(data_dir)) {
            for (const auto& entry : std::filesystem::directory_iterator(data_dir)) {
                CMString fname = entry.path().filename().string();
                if (fname.size() >= 4 &&
                    fname.substr(0, 5) == "data_" &&
                    fname.substr(fname.size() - 4) == ".dat") {
                    std::error_code ec;
                    std::filesystem::remove(entry.path(), ec);
                    if (!ec) {
                        ++deleted;
                    } else {
                        WARN("DeleteData: failed to remove {}: {}", entry.path().string(), ec.message());
                    }
                }
            }
        }

        deleted_writers = msg.writer_ids_;  // 记录请求的 writer_ids（实际删了全部 dat）
        ack.success_ = true;
        ack.deleted_count_ = deleted;
        ack.deleted_writer_ids_ = std::move(deleted_writers);
        INFO("DeleteData complete: db_id={}, removed {} .dat files from {}",
             msg.db_id_, deleted, data_dir);
    } catch (const std::exception& e) {
        ack.success_ = false;
        ack.error_message_ = e.what();
        ERR("DeleteData failed: db_id={}, error={}", msg.db_id_, e.what());
    }

    reactor_->send(conn_id, ack);
}

void WorkerAgent::on_merge_cleanup(uint64_t conn_id, const MergeCleanupMessage& msg) {
    touch_master_contact();
    auto ds = DataService::instance();

    // 检查本 worker 是否在免清理列表（merge target worker 已持有效 local_idx，跳过清理）。
    bool exempt = false;
    for (uint64_t wid : msg.exempt_worker_ids_) {
        if (wid == worker_id_) { exempt = true; break; }
    }

    if (!exempt) {
        // 1. 清旧索引（指向已删源 .dat / 失效源 worker 位置）。
        //    不碰 ObjectCache（数据内容未变，cache 仍是正确副本）。
        ds->clear_local_index_for_db(msg.db_id_);
        ds->clear_remote_index_for_db(msg.db_id_);

        // 2. 更新 db_paths_ 指向 merge 后的新路径。
        ds->register_database(msg.db_id_, msg.base_path_, msg.data_path_, "");

        // 3. 尝试 load 新 idx 重建 local_idx（新 idx 由 merge worker 写在共享 base_path）。
        //    若 data_path 可达（同机本地盘或共享 FS），后续读可本地直读 .dat，不走远程读。
        int32_t loaded = 0;
        try {
            namespace fs = std::filesystem;
            if (fs::exists(msg.base_path_)) {
                for (const auto& entry : fs::directory_iterator(msg.base_path_)) {
                    CMString fname = entry.path().filename().string();
                    if (fname.size() >= 4 &&
                        fname.substr(fname.size() - 4) == ".idx") {
                        LocalIndex idx(entry.path().string());
                        idx.load();
                        auto entries = idx.get_all_entries();
                        if (!entries.empty()) {
                            ds->restore_entries(msg.db_id_, entries);
                            ++loaded;
                        }
                    }
                }
            }
        } catch (const std::exception& e) {
            WARN("MergeCleanup: failed to load idx from {}: {}", msg.base_path_, e.what());
        }
        INFO("MergeCleanup: db_id={}, cleared old idx, loaded {} new idx files, "
             "base={} data={} on worker_id={}",
             msg.db_id_, loaded, msg.base_path_, msg.data_path_, worker_id_);
    } else {
        INFO("MergeCleanup: worker_id={} exempt (merge target), keeping state for db_id={}",
             worker_id_, msg.db_id_);
    }

    // 无论 exempt 与否，都回 MergeCleanupAck（master 的全局一致性屏障需要收齐所有 worker）。
    MergeCleanupAckMessage ack;
    ack.worker_id_ = worker_id_;
    ack.db_id_ = msg.db_id_;
    reactor_->send(conn_id, ack);
}

}  // namespace fly
