#include <agent/cpp/worker_agent.h>
#include <agent/cpp/graceful_shutdown.h>
#include <log/cpp/logger.h>
#include <message/cpp/message_macros.h>
#include <message/cpp/message_registry.h>
#include <core/cpp/config.h>
#include <core/cpp/system_info.h>
#include <sstream>
#include <iomanip>
#include <core/cpp/process_info.h>
#include <core/cpp/graceful_exit.h>
#include <storage/cpp/data_service.h>
#include <common/cpp/write_context_hash.h>
#include <network/cpp/data_client_pool.h>
#include <network/cpp/metadata_client.h>
#include <network/cpp/tcp_socket.h>
#include <network/cpp/message_protocol.h>
#include <network/cpp/net_quality_monitor.h>
#include <thread>
#include <chrono>
#include <functional>
#include <unistd.h>
#include <spawn.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <dirent.h>
#include <cstdlib>
#include <vector>

extern char** environ;

namespace fly {

WorkerAgent::WorkerAgent(uint64_t worker_id, const CMString& master_host, uint16_t master_port,
                          const CMVector<CMString>& attributes, const CMString& role)
    : worker_id_(worker_id), master_host_(master_host), master_port_(master_port),
      attributes_(attributes), running_(false), registered_(false) {
    // role 静态身份（注册时设定，不可变更）：仅 hybrid / storage_only；
    // 非法值 WARN 回退 hybrid。
    if (role == "storage_only") {
        role_ = static_cast<uint8_t>(WorkerRole::STORAGE_ONLY);
    } else {
        if (!role.empty() && role != "hybrid") {
            WARN("Unknown worker role '{}' (expected hybrid|storage_only), "
                 "falling back to hybrid", role);
        }
        role_ = static_cast<uint8_t>(WorkerRole::HYBRID);
    }
}

WorkerAgent::~WorkerAgent() {
    stop();
}

// connect master 指数退避重试：initial=worker_connect_retry_initial_ms（默认
// 500ms），倍率 ×2，单次 sleep 上限 10s；总保活窗口 = worker_register_timeout
//（与 master 占位符共用一键，默认 300s=5min；0=无限重试）。
uint64_t WorkerAgent::connect_master_with_retry(ConnectionManager& transport) {
    const int64_t keepalive_s = Config::instance()->get_int("worker_register_timeout");
    int64_t initial_ms = Config::instance()->get_int("worker_connect_retry_initial_ms");
    if (initial_ms <= 0) initial_ms = 500;
    const bool infinite = (keepalive_s <= 0);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(keepalive_s);

    int64_t delay_ms = initial_ms;
    int attempt = 0;
    while (true) {
        ++attempt;
#ifdef FLY_ENABLE_TEST_HOOKS
        connect_attempts_for_testing_.push_back(std::chrono::steady_clock::now());
#endif
        uint64_t conn = transport.connect(master_host_, master_port_);
        if (conn != 0) return conn;
        if (!infinite && std::chrono::steady_clock::now() >= deadline) {
            return 0;
        }
        WARN("connect to master {}:{} failed (attempt {}), retry in {}ms{}",
             master_host_, master_port_, attempt, delay_ms,
             infinite ? "" : " (keepalive budget)");
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        delay_ms = std::min<int64_t>(delay_ms * 2, 10000);
    }
}

void WorkerAgent::start() {
    if (running_) return;

    shutdown_triggered_ = false;


    auto transport = create_connection_manager("tcp");
    if (!transport || !transport->listen("0.0.0.0", 0)) {
        // transport 创建/监听失败与 master 连接失败同级：worker 无法服务，
        // 干净退出 start()（running_ 保持 false），调用方经 is_running() 观察。
        ERR("Worker transport listen failed — worker cannot start");
        return;
    }
    // connect 指数退避重试：覆盖瞬时网络抖动与 master 短时过载（listen backlog
    // 满导致的拒绝/超时）。领域约束：master 挂 = 全群失败，不做"等 master 出现"
    // 的长期等待——总保活窗口与 master 占位符共用 worker_register_timeout
    //（两侧统一 5min；0=无限）。master 中途断连（on_disconnect）不重连。
    master_conn_ = connect_master_with_retry(*transport);
    if (master_conn_ == 0) {
        // Connection failure is non-fatal at the network layer; for a worker it
        // is fatal: a worker cannot run without its master. Abort start() cleanly
        // (running_ stays false, no reactor/data-server created) and let the caller
        // observe is_running()==false and exit.
        ERR("Failed to connect to master {}:{} after retries — worker cannot start",
            master_host_, master_port_);
        return;
    }
    INFO("connected, master_conn={}", master_conn_.load());
    // 与 master 对称：handler lane 并行（同连接保序）。worker 连接少（master+peers），
    // lane 上界由配置控制。
    size_t handler_lanes = static_cast<size_t>(Config::instance()->get_int("handler_lanes"));
    reactor_ = CMMakeUnique<Reactor>(std::move(transport), handler_lanes);

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
            on_register_ack(conn, msg);
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

    reactor_->register_handler<StorageSpawnRequestMessage>(
        [this](uint64_t conn_id, const StorageSpawnRequestMessage& msg) {
            on_storage_spawn_request(conn_id, msg);
        });

    reactor_->register_handler<WorkerProbeMessage>(
        [this](uint64_t conn_id, const WorkerProbeMessage& msg) {
            // master 的重复注册活性探测：应答即证明本实例活着。
            WorkerProbeAckMessage ack;
            ack.worker_id_ = msg.worker_id_;
            reactor_->send(conn_id, ack);
            DBG("WorkerProbe answered: worker_id={}", msg.worker_id_);
        });

    reactor_->register_handler<TaskSubmitAckMessage>(
        [this](uint64_t conn_id, const TaskSubmitAckMessage& msg) {
            // 转发提交的入图确认：唤醒同步等待的提交方（带回 task_id）。
            pending_task_submits_.complete(msg.request_id_, [&](PendingTaskSubmit& p) {
                p.accepted_ = msg.accepted_;
                p.task_id_ = msg.task_id_;
                p.completed_ = true;
            });
            DBG("TaskSubmitAck: request_id={}, task_id={}, accepted={}",
                msg.request_id_, msg.task_id_, msg.accepted_);
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

    reactor_->register_handler<MessageLimitSyncMessage>(
        [this](uint64_t conn_id, const MessageLimitSyncMessage& msg) {
            on_message_limit_sync(conn_id, msg);
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

    send_register_message();

    heartbeat_running_ = true;
    heartbeat_thread_ = std::thread([this] { heartbeat_loop(); });

    // 注册守望：RegisterAck 的事件驱动等待 + 超时退避重发（P3-23 兜底）。
    register_watchdog_running_ = true;
    register_watchdog_thread_ = std::thread([this] { register_watchdog_loop(); });

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

    // 关闭业务 RPC 端口（如已启动）。
    if (peer_rpc_server_) {
        peer_rpc_server_->stop();
        peer_rpc_server_.reset();
        peer_rpc_port_ = 0;
    }

    // 重连线程先于 reactor join（它在 connect 上自旋时依赖 reactor 存活；
    // initiate_shutdown 置 running_=false 已让它退出）。
    reconnecting_.store(false);
    if (reconnect_thread_.joinable()) {
        reconnect_thread_.join();
    }

    // 注册守望（initiate_shutdown 已置位+notify，此处仅回收）。
    if (register_watchdog_thread_.joinable()) {
        register_watchdog_thread_.join();
    }

    if (heartbeat_thread_.joinable()) {
        heartbeat_thread_.join();
    }

    if (probe_thread_.joinable()) {
        probe_thread_.join();
    }

    if (reactor_thread_.joinable()) {
        reactor_thread_.join();
    }
    // reset 前等 lane 排空：reset 先置空 reactor_ 再析构，迟到的 handler 经本
    // 对象 reactor_ 访问会解引用空指针（IdxLoad 空文件用例实测崩溃点）。
    if (reactor_) {
        reactor_->drain_handlers();
    }
    reactor_.reset();

    {
        std::unique_lock<std::shared_mutex> db_lk(databases_mutex_);
        databases_.clear();
    }

    DataService::instance()->stop_data_server();

    running_ = false;
    registered_ = false;
}

bool WorkerAgent::is_running() const {
    // SIGTERM 信号灯：Python poll 循环（每 100ms）观察到 false 即退出，
    // 随后 main.py 调 agent.stop() 走完整清理。
    return running_ && !graceful_shutdown_signalled();
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

uint64_t WorkerAgent::submit_task(const CMString& name, const CMString& module,
                               const CMVector<CMString>& args,
                               const CMVector<CMString>& inputs,
                               const CMVector<CMString>& required_capabilities,
                               float attribute_timeout,
                               const CMString& write_context_hash,
                               const CMVector<CMString>& vars,
                               int priority) {
    // No reactor means start() failed (e.g. master unreachable) — nothing to
    // send to. Fail soft rather than crash; caller observes no progress.
    if (!reactor_) {
        ERR("submit_task '{}' ignored: worker not started (no reactor)", name);
        return 0;
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
    msg.priority_ = priority;
    msg.request_id_ = next_submit_request_id_.fetch_add(1);

    auto pending = CMMakeShared<PendingTaskSubmit>();
    pending_task_submits_.emplace(msg.request_id_, pending);

    // Ack 强语义（用户确认语义）：同步 RPC——master 入图确认（带回 task_id）
    // 才放行，杜绝 task 体内提交静默蒸发（fanout 场景断连丢失子任务且调用
    // 方不知情、下游依赖永不就绪）。断连窗口按 A 类挂起：入统一重放队列、
    // 阻塞等注册确认后重放拿 Ack；worker 终止批量 fail。
    if (!registered_.load()) {
        enqueue_master_send([this, msg](uint64_t conn) { reactor_->send(conn, msg); });
        WARN("TaskSubmit pending (not registered): task={} — caller blocks until "
             "registration confirms", name);
        auto result = pending_task_submits_.wait_for(msg.request_id_, std::chrono::seconds(300),
            [](const CMSharedPtr<PendingTaskSubmit>& p) { return p->completed_; });
        pending_task_submits_.erase(msg.request_id_);
        if (result && result->accepted_) return result->task_id_;
        ERR("TaskSubmit failed (pending path): task={}", name);
        return 0;
    }

    reactor_->send(master_conn_, msg);

    auto result = pending_task_submits_.wait_for(msg.request_id_, std::chrono::seconds(5),
        [](const CMSharedPtr<PendingTaskSubmit>& p) { return p->completed_; });
    pending_task_submits_.erase(msg.request_id_);
    if (result && result->accepted_) return result->task_id_;
    ERR("TaskSubmit not acknowledged: task={} (timeout or rejected)", name);
    return 0;
}

void WorkerAgent::send_register_message() {
    RegisterMessage reg;
    reg.worker_id_ = worker_id_;
    reg.attributes_ = attributes_;
    reg.data_server_host_ = data_server_host_;
    reg.data_server_port_ = data_server_port_;
    reg.hostname_ = ProcessInfo::instance()->hostname();
    reg.ip_address_ = data_server_host_;
    reg.role_ = role_;
    reactor_->send(master_conn_, reg);

    auto dsp = data_server_port_;
    auto attr_count = attributes_.size();
    INFO("RegisterMessage sent with data_server_port={}, attributes={}, role={}",
         dsp, attr_count, role_ == static_cast<uint8_t>(WorkerRole::STORAGE_ONLY)
                               ? "storage_only" : "hybrid");
}

void WorkerAgent::register_watchdog_loop() {
    // 指数退避：initial（默认 500ms）×2，单次上限 30s。覆盖「master 活着但
    // 注册/ack 被应用层吞掉」——连接级丢失由 on_disconnect 的事件驱动
    // reconnect_loop 恢复（毫秒级），本循环在 reconnecting_ 期间让位。
    int64_t delay_ms = Config::instance()->get_int("worker_register_ack_retry_initial_ms");
    if (delay_ms <= 0) delay_ms = 500;
    const int64_t kMaxDelayMs = 30000;

    while (register_watchdog_running_) {
        std::unique_lock<std::mutex> lk(register_ack_mutex_);
        register_ack_cv_.wait_for(lk, std::chrono::milliseconds(delay_ms),
                                  [this] {
                                      return !register_watchdog_running_.load()
                                             || registered_.load();
                                  });
        if (!register_watchdog_running_ || registered_) break;

        // ack 未到：幂等重发（master 对同 conn 重发走正常注册路径）。
        // 重连进行中（reconnect_loop 负责连接+注册+ack 等待）则让位。
        if (!reconnecting_.load() && running_.load() && master_conn_.load() != 0) {
            WARN("No RegisterAck — resending REGISTER (backoff {}ms; "
                 "ack may have been lost at application layer)", delay_ms);
            send_register_message();
            delay_ms = std::min(delay_ms * 2, kMaxDelayMs);
        } else {
            // reconnect 期间不重置退避节奏：按当前 delay 继续守望，注册
            // 成功由 cv predicate 捕获。
        }
    }
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

void WorkerAgent::on_register_ack(uint64_t conn_id, const RegisterAckMessage& msg) {
    // 重复注册被拒（master 侧同 worker_id 已有活跃实例——分区恢复/手动重启
    // 竞态的先到先得判定）：本实例是后到者，自行干净退出。
    if (msg.duplicate_) {
        ERR("RegisterAck: worker_id {} already active on master (duplicate) — exiting",
            worker_id_);
        initiate_shutdown("duplicate worker id rejected by master");
        return;
    }

    registered_ = true;
    touch_master_contact();
    // 注册守望退出（事件驱动，无空转等待）。
    {
        std::lock_guard<std::mutex> lk(register_ack_mutex_);
        register_ack_cv_.notify_all();
    }

    INFO("RegisterAck received, registered");
    bool was_reconnecting = reconnecting_.exchange(false);
    if (was_reconnecting) {
        INFO("Reconnection complete — resuming heartbeats, flushing buffered reports");
    }
    // 注册成功后按序重放缓冲消息（用户确认语义：注册完成前的消息一律
    // pending，注册后重放）：
    //   1. 统一重放队列（A 类同步 RPC + B 类通知，FIFO = 语义序，含
    //      WriteRegister——streaming 模式 master 靠它登记位置）；
    //   2. task 上报 flush（固定最后——首连 assign-抢在-Ack-前窗口或断连
    //      窗口缓冲的 complete/failed）。
    replay_pending_master_sends();
    flush_pending_reports();
}

void WorkerAgent::enqueue_master_send(std::function<void(uint64_t conn)> replay) {
    std::lock_guard<std::mutex> lk(pending_master_sends_mutex_);
    pending_master_sends_.push_back(PendingMasterSend{std::move(replay)});
}

void WorkerAgent::replay_pending_master_sends() {
    CMVector<PendingMasterSend> to_replay;
    {
        std::lock_guard<std::mutex> lk(pending_master_sends_mutex_);
        to_replay = std::move(pending_master_sends_);
        pending_master_sends_.clear();
    }
    if (to_replay.empty()) return;
    INFO("Replaying {} buffered master-bound message(s) after registration",
         to_replay.size());
    uint64_t conn = master_conn_.load();
    for (auto& send : to_replay) {
        if (send.replay_) send.replay_(conn);
    }
}

void WorkerAgent::on_task_assign(const TaskAssignMessage& msg) {
    touch_master_contact();

    // Pre-fetched dependency locations go straight into the persistent
    // remote_idx (single source of truth) so the first read hits TIER2 instead
    // of falling through to a TIER3 master query. dependency_locations_ already
    // carries all replicas.
    if (!msg.dependency_locations_.empty()) {
        for (const auto& loc : msg.dependency_locations_) {
            DataService::instance()->update_remote_idx(loc.object_name, loc.worker_id, loc.host,
                                                        loc.port, 0, loc.storage_only != 0);
            DBG("[PREFETCH] obj={} worker_id={} host={} port={} storage_only={}",
                loc.object_name, loc.worker_id, loc.host, loc.port, loc.storage_only);
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
        // notify 持锁：防 lost wakeup（waiter 持锁查 pred 与进入 wait 之间的
        // 窗口里无锁 notify 会落空），同 8419526 修复。
        task_queue_cv_.notify_one();
    }
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
                // dirty_objects_ 是全名列表（master 据此清理 remote_idx/provenance）。
                for (const auto& w : tracked_writes) failed.dirty_objects_.push_back(w.full_name_);
                send_master_or_buffer(failed);

                ERR("Task marked failed: task_id={}, write error_type={}",
                    task.task_id_, static_cast<int>(error_type));
            } else {
                // 成功：对所有涉及的 db 打 END（提交写入段）。
                commit_task_segments(tracked_writes);

                TaskCompleteMessage complete;
                complete.task_id_ = task.task_id_;
                complete.worker_id_ = worker_id_;
                // 实际写出对象（含 size）：size 直接取自 WriteRecord（原并行 map 在
                // end_task 中被清空导致恒为 0 的死代码，现已随容器合并修复）。
                for (const auto& w : tracked_writes) {
                    complete.written_objects_.push_back({w.full_name_, w.size_bytes_});
                }
                // 声明性输出（task 装饰器声明，非实际 write，size=0）。
                for (auto& out : result.outputs_) {
                    complete.written_objects_.push_back({std::move(out), 0});
                }
                complete.frozen_dbs_ = std::move(result.frozen_dbs_);
                send_master_or_buffer(complete);

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
            for (const auto& w : tracked_writes) failed.dirty_objects_.push_back(w.full_name_);
            send_master_or_buffer(failed);

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
    if (conn_id != master_conn_.load()) return;

    // 网络闪断（master 挂=全群失败，但闪断不是挂）：宽限窗口内指数退避重连，
    // task 在本 worker 上继续执行（master 侧 task 存活、宽限内不判死）。
    // worker_reconnect_timeout=0（逃生口）维持旧的"断连即死"行为。
    int64_t grace = Config::instance()->get_int("worker_reconnect_timeout");
    if (grace <= 0) {
        WARN("Master connection lost, shutting down (reconnect disabled)");
        initiate_shutdown("master connection lost");
        return;
    }

    if (reconnecting_.exchange(true)) {
        return;  // 重连线程已在跑（connect 自旋或 ack 等待中），连环闪断由它处理
    }
    WARN("Master connection lost — reconnecting with exponential backoff "
         "(grace {}s, tasks keep running)", grace);
    registered_ = false;   // 心跳暂停；RegisterAck 恢复
    if (reconnect_thread_.joinable()) {
        reconnect_thread_.join();  // 上一轮已结束未回收（join 立即返回）
    }
    reconnect_thread_ = std::thread([this] { reconnect_loop(); });
}

void WorkerAgent::reconnect_loop() {
    const int64_t grace_s = Config::instance()->get_int("worker_reconnect_timeout");
    int64_t initial_ms = Config::instance()->get_int("worker_connect_retry_initial_ms");
    if (initial_ms <= 0) initial_ms = 500;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(grace_s);

    int64_t delay_ms = initial_ms;
    int attempt = 0;
    // 外层循环常驻直到 RegisterAck 确认（reconnecting_ 清除）或宽限耗尽——
    // 连接成功但 ack 未到又断连的连环闪断由同一线程自然处理（ack 等待超时
    // 后落回重试），无需 on_disconnect 重启线程。
    while (reconnecting_.load() && running_.load()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            reconnecting_.store(false);
            ERR("Reconnect to master {}:{} failed within {}s grace — giving up",
                master_host_, master_port_, grace_s);
            initiate_shutdown("master reconnect grace expired");
            return;
        }

        ++attempt;
        uint64_t conn = reactor_->connect(master_host_, master_port_);
        if (conn == 0) {
            WARN("Reconnect attempt {} to master {}:{} failed, retry in {}ms",
                 attempt, master_host_, master_port_, delay_ms);
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
            delay_ms = std::min<int64_t>(delay_ms * 2, 10000);
            continue;
        }

        master_conn_ = conn;
        WARN("Reconnected to master (attempt {}), re-registering as worker {}",
             attempt, worker_id_);
        RegisterMessage reg;
        reg.worker_id_ = worker_id_;
        reg.attributes_ = attributes_;
        reg.data_server_host_ = data_server_host_;
        reg.data_server_port_ = data_server_port_;
        reg.hostname_ = ProcessInfo::instance()->hostname();
        reg.ip_address_ = "";
        reg.role_ = role_;   // 静态身份：重连同值上报
        reactor_->send(conn, reg);

        // 等 RegisterAck（on_register_ack 清除 reconnecting_ 并 flush）。
        // 超时未确认（连接又断 / master 未回）→ 落回重试循环。
        const auto ack_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (reconnecting_.load() && running_.load() &&
               std::chrono::steady_clock::now() < ack_deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        delay_ms = initial_ms;  // 新一轮退避从头开始
    }
}

void WorkerAgent::send_master_or_buffer(const TaskCompleteMessage& msg) {
    if (!reconnecting_.load() && registered_.load()) {
        reactor_->send(master_conn_.load(), msg);
        return;
    }
    std::lock_guard<std::mutex> lk(pending_reports_mutex_);
    pending_reports_.push_back({true, msg, TaskFailedMessage{}});
    WARN("TaskComplete buffered (reconnecting): task_id={}", msg.task_id_);
}

void WorkerAgent::send_master_or_buffer(const TaskFailedMessage& msg) {
    if (!reconnecting_.load() && registered_.load()) {
        reactor_->send(master_conn_.load(), msg);
        return;
    }
    std::lock_guard<std::mutex> lk(pending_reports_mutex_);
    pending_reports_.push_back({false, TaskCompleteMessage{}, msg});
    WARN("TaskFailed buffered (reconnecting): task_id={}", msg.task_id_);
}

void WorkerAgent::flush_pending_reports() {
    CMVector<PendingReport> to_send;
    {
        std::lock_guard<std::mutex> lk(pending_reports_mutex_);
        to_send = std::move(pending_reports_);
        pending_reports_.clear();
    }
    if (to_send.empty()) return;
    INFO("Flushing {} buffered task report(s) after reconnect", to_send.size());
    for (const auto& r : to_send) {
        if (r.is_complete_) {
            reactor_->send(master_conn_.load(), r.complete_);
        } else {
            reactor_->send(master_conn_.load(), r.failed_);
        }
    }
}

#ifdef FLY_ENABLE_TEST_HOOKS
size_t WorkerAgent::pending_report_count_for_testing() {
    std::lock_guard<std::mutex> lk(pending_reports_mutex_);
    return pending_reports_.size();
}
#endif

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
    // 三处 notify 均持对应锁发出（predicate 虽读 atomic flag，但无锁 notify 仍存在
    // lost wakeup 窗口，waiter 只能靠超时兜底退出）。
    {
        std::lock_guard<std::mutex> lk(register_ack_mutex_);
        register_watchdog_running_ = false;
        register_ack_cv_.notify_all();
    }
    {
        std::lock_guard<std::mutex> lock(heartbeat_mutex_);
        heartbeat_running_ = false;
        heartbeat_cv_.notify_all();
    }
    {
        std::lock_guard<std::mutex> lock(probe_mutex_);
        probe_running_ = false;
        probe_cv_.notify_all();
    }
    {
        std::lock_guard<std::mutex> lock(task_queue_mutex_);
        task_queue_cv_.notify_all();
    }
    // 阻塞在「未注册窗口 A 类挂起」的全部同步 RPC：终局唤醒（重连失败/
    // worker 退出即失败——用户确认语义的两个终态之一）。
    pending_write_regs_.complete_all_if(
        [](const PendingWriteRegister&) { return true; },
        [](PendingWriteRegister& p) {
            p.completed_ = true;
            p.success_ = false;
            p.error_type_ = TaskErrorType::WRITE_REGISTRATION_FAILED;
            p.error_message_ = "worker shutting down before registration confirmed";
        });
    pending_db_paths_.complete_all_if(
        [](const PendingDbPath&) { return true; },
        [](PendingDbPath& p) {
            p.completed_ = true;
            p.success_ = false;
        });
    pending_freezes_.complete_all_if(
        [](const PendingFreezeAck&) { return true; },
        [](PendingFreezeAck& p) {
            p.completed_ = true;
            p.success_ = false;
            p.error_type_ = TaskErrorType::WRITE_REGISTRATION_TIMEOUT;
        });
    pending_var_ops_.complete_all_if(
        [](const PendingVarOp&) { return true; },
        [](PendingVarOp& p) {
            p.completed_ = true;
            p.success_ = false;
            p.error_message_ = "worker shutting down before registration confirmed";
        });
    pending_removes_.complete_all_if(
        [](const PendingRemove&) { return true; },
        [](PendingRemove& p) {
            p.completed_ = true;
            p.success_ = false;
        });
    pending_task_submits_.complete_all_if(
        [](const PendingTaskSubmit&) { return true; },
        [](PendingTaskSubmit& p) {
            p.completed_ = true;
            p.accepted_ = false;
            p.task_id_ = 0;
        });
    // B 类重放队列：worker 终止，不再重放（丢弃通知类消息；A 类的等待者
    // 已由上面的批量 fail 唤醒，重放闭包无需执行）。
    {
        std::lock_guard<std::mutex> lk(pending_master_sends_mutex_);
        pending_master_sends_.clear();
    }
    if (reactor_) {
        reactor_->stop();
    }
}

void WorkerAgent::on_db_path_response(const DbPathResponseMessage& msg) {
    touch_master_contact();
    pending_db_paths_.complete(msg.db_path_, [&](PendingDbPath& p) {
        p.db_path_ = msg.db_path_;
        p.data_path_ = msg.data_path_;
        p.success_ = msg.success_;
        p.completed_ = true;
    });
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
    WorkerAgentContext::set_record_write_func([this](const CMString& db_path, const CMString& name, int64_t size) {
        record_write(db_path, name, size);
    });
    WorkerAgentContext::set_register_func([this](const CMString& db_path, const CMString& name, int64_t size) -> std::pair<CMString, TaskErrorType> {
        return register_write_with_master(db_path, name, size);
    });
    WorkerAgentContext::set_notify_removed_func([this](const CMString& db_path, const CMString& name) {
        CMString full_name = db_path + ":" + name;
        ObjectRemovedMessage msg;
        msg.object_name_ = full_name;
        msg.db_path_ = db_path;
        // B 类（遗留通知路径，无生产调用者——db.remove_object 实际走
        // request_object_remove 同步链路）：防御性入队，断连窗口不丢。
        if (!registered_.load()) {
            enqueue_master_send([this, msg](uint64_t conn) { reactor_->send(conn, msg); });
            WARN("ObjectRemoved pending (not registered): {} — replays after "
                 "registration", full_name);
            return;
        }
        reactor_->send(master_conn_, msg);
        INFO("ObjectRemoved sent to master: {}", full_name);
    });
    WorkerAgentContext::set_freeze_func([this](const CMString& db_path) {
        request_database_freeze(db_path);
    });
    WorkerAgentContext::set_remove_request_func([this](const CMString& db_path, const CMString& object_name) {
        request_object_remove(db_path, object_name);
    });
    WorkerAgentContext::set_backup_request_func([this](const CMString& db_path, const CMString& object_name) {
        request_backup(db_path, object_name);
    });
    // Backup suggest：worker TIER2 读流量达阈值后上报增量给 master 聚合判定。
    WorkerAgentContext::set_suggest_backup_func(
        [this](const CMString& object_name, uint64_t delta_count, uint64_t delta_bytes, int64_t size_bytes) {
            WorkerBackupSuggestMessage msg;
            msg.worker_id_ = worker_id_;
            msg.object_name_ = object_name;
            msg.delta_count_ = delta_count;
            msg.delta_bytes_ = delta_bytes;
            msg.size_bytes_ = size_bytes;
            reactor_->send(master_conn_, msg);
        });
    // Var funcs: route to master over the network (synchronous set/get).
    // Names are FULL (db_path:short_name); forwarded as-is over the wire.
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

void WorkerAgent::record_write(const CMString& db_path, const CMString& object_name, int64_t size) {
    CMString full_name = db_path + ":" + object_name;
    current_writes_.push_back({full_name, size});
}

CMVector<WriteRecord> WorkerAgent::end_task(uint64_t task_id) {
    WorkerAgentContext::clear();
    WorkerAgentContext::clear_current_write_hash();
    current_write_hash_.clear();
    auto writes = std::move(current_writes_);
    current_writes_.clear();
    current_task_id_ = 0;
    return writes;
}

void WorkerAgent::commit_task_segments(const CMVector<WriteRecord>& written_objects) {
    // task 成功：对所有涉及写入的 db 打 END，提交写入段。
    // 段未开（db 无写入）的 mark_write_end 是 no-op（DataWriter::segment_active_==false）。
    CMUnorderedSet<CMString> involved_dbs;
    for (const auto& w : written_objects) {
        auto [db_path, short_name] = fly::split_full_name(w.full_name_);
        if (!db_path.empty()) {
            involved_dbs.insert(db_path);
        }
    }
    for (const auto& db_path : involved_dbs) {
        CMSharedPtr<Database> db;
        {
            // 锁内只 find + 拷 shared_ptr：drain（等磁盘写回）与 mark_write_end
            // 移出容器锁（D3 拆除——原持读锁等 WBQ drain）。
            std::shared_lock<std::shared_mutex> db_lk(databases_mutex_);
            auto it = databases_.find(db_path);
            if (it != databases_.end()) {
                db = it->second;
            }
        }
        if (db) {
            // 先 drain 保证段内所有 ADD 已落盘，再打 END 提交。
            fly::DataService::instance()->drain_write_back();
            db->mark_write_end();
        }
    }
}

void WorkerAgent::cleanup_failed_task_writes(const CMVector<WriteRecord>& dirty_objects) {
    // task 失败：按 db_path 分组，对每个 db 调 abort_task_writes 撤销写入。
    // idx 打 ABORT（整段 pending 撤销）+ data 文件 truncate 回滚 + 清运行时内存。
    CMUnorderedMap<CMString, CMVector<CMString>> by_db;
    for (const auto& w : dirty_objects) {
        auto [db_path, short_name] = fly::split_full_name(w.full_name_);
        if (!db_path.empty()) {
            by_db[db_path].push_back(w.full_name_);
        }
    }
    for (auto& [db_path, full_names] : by_db) {
        CMSharedPtr<Database> db;
        {
            // 锁内只 find + 拷 shared_ptr：abort（drain + truncate + 清内存）
            // 移出容器锁（D3 拆除）。
            std::shared_lock<std::shared_mutex> db_lk(databases_mutex_);
            auto it = databases_.find(db_path);
            if (it == databases_.end()) continue;
            db = it->second;
        }
        db->abort_task_writes(full_names);
    }
}

void WorkerAgent::request_database_freeze(const CMString& db_path) {
    // 同步等 ack（仿 register_write_with_master 的 pending+cv 模式）：
    // 非 stream 模式 master 登记 pending；冲突时回 DB_ALREADY_FROZEN 联动 task 失败。
    auto pending = CMMakeShared<PendingFreezeAck>();
    pending->db_path_ = db_path;
    pending_freezes_.emplace(db_path, pending);

    DatabaseFreezeNotification msg;
    msg.db_path_ = db_path;
    msg.task_id_ = current_task_id_;   // 非 stream 模式 master 登记 pending 需要
    if (!registered_.load()) {
        // A 类挂起（用户确认语义）：master 裁决 DB_ALREADY_FROZEN 冲突、被拒
        // 必须联动 task 失败——放行 = 冲突未检出。断连窗口不发送（必丢失），
        // 入统一重放队列、阻塞等注册确认后重放拿 FreezeAck。
        enqueue_master_send([this, msg](uint64_t conn) { reactor_->send(conn, msg); });
        WARN("Freeze pending (not registered): db_path={} — caller blocks until "
             "registration confirms", db_path);
        auto result = pending_freezes_.wait_for(db_path, std::chrono::seconds(300),
            [](const CMSharedPtr<PendingFreezeAck>& p) { return p->completed_; });
        pending_freezes_.erase(db_path);
        if (result && result->success_) {
            INFO("Freeze acked (after replay): db_path={}", db_path);
        } else if (result) {
            WorkerAgentContext::set_last_error_type(result->error_type_);
            ERR("Freeze rejected (after replay): db_path={}, error_type={}", db_path,
                static_cast<int>(result->error_type_));
        } else {
            WorkerAgentContext::set_last_error_type(TaskErrorType::WRITE_REGISTRATION_TIMEOUT);
            ERR("Freeze pending timeout: db_path={}", db_path);
        }
        return;
    }

    reactor_->send(master_conn_, msg);
    INFO("Freeze notification sent: db_path={}, task_id={}", db_path, current_task_id_);

    auto result = pending_freezes_.wait_for(db_path, std::chrono::seconds(5),
        [](const CMSharedPtr<PendingFreezeAck>& p) { return p->completed_; });
    pending_freezes_.erase(db_path);
    if (!result) {
        // 超时（master 无响应）→ 当失败处理，联动 task 失败
        WorkerAgentContext::set_last_error_type(TaskErrorType::WRITE_REGISTRATION_TIMEOUT);
        ERR("Freeze ack timeout: db_path={}", db_path);
        return;
    }
    if (!pending->success_) {
        // 冲突（DB_ALREADY_FROZEN）→ 联动 task 失败（poll_task 检查 last_error_type）
        WorkerAgentContext::set_last_error_type(pending->error_type_);
        ERR("Freeze rejected: db_path={}, error_type={}", db_path,
            static_cast<int>(pending->error_type_));
        return;
    }
    INFO("Freeze acked: db_path={}", db_path);
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
        DataService::instance()->update_remote_idx(object_name, rl.worker_id_, rl.host_,
                                                    rl.port_, 0, rl.storage_only_);
    }

    return {true, location.can_still_produce_};
}

void WorkerAgent::register_database(const CMString& db_path, CMSharedPtr<Database> db) {
    std::unique_lock<std::shared_mutex> db_lk(databases_mutex_);
    databases_[db_path] = std::move(db);
}

bool WorkerAgent::request_db_path(const CMString& db_path) {
    // 锁只覆盖 find：后面 wait_for 要等 master 响应（lane 线程处理响应时需拿
    // 写锁插入 databases_），持读锁等待 = 自死锁。
    {
        std::shared_lock<std::shared_mutex> db_lk(databases_mutex_);
        auto it = databases_.find(db_path);
        if (it != databases_.end()) {
            return true;
        }
    }
    auto pending = CMMakeShared<PendingDbPath>();
    pending->db_path_ = db_path;
    pending_db_paths_.emplace(db_path, pending);

    DbPathRequestMessage req;
    req.db_path_ = db_path;
    if (!registered_.load()) {
        // A 类挂起（用户确认语义）：db_path 是 master 分配的权威路径，后续
        // 全部写操作依赖——断连窗口不发送（发到死连接必丢失），入统一重放
        // 队列、阻塞等注册确认后重放拿 Ack。原 5s 超时在断连场景只会把
        // 宽限内存活的 task 错误判死。
        enqueue_master_send([this, req](uint64_t conn) { reactor_->send(conn, req); });
        WARN("DbPathRequest pending (not registered): db_path={} — caller blocks "
             "until registration confirms", db_path);
        auto result = pending_db_paths_.wait_for(db_path, std::chrono::seconds(300),
            [](const CMSharedPtr<PendingDbPath>& p) { return p->completed_; });
        pending_db_paths_.erase(db_path);
        if (result && result->success_ && !result->db_path_.empty()) {
            auto db = CMMakeShared<Database>(result->db_path_, result->data_path_,
                                             worker_id_, data_server_host_, db_path);
            {
                std::unique_lock<std::shared_mutex> db_lk(databases_mutex_);
                databases_[db_path] = db;
            }
            return true;
        }
        return false;
    }

    reactor_->send(master_conn_, req);

    INFO("Sent DbPathRequest for db_path={}", db_path);

    auto result = pending_db_paths_.wait_for(db_path, std::chrono::seconds(5),
        [](const CMSharedPtr<PendingDbPath>& p) { return p->completed_; });
    pending_db_paths_.erase(db_path);
    if (result && result->success_ && !result->db_path_.empty()) {
        // Reuse the master-assigned db_path instead of generating a
        // fresh random one. Without this, the worker's Database
        // would get a different db_path than the master recorded,
        // so object names (db_path:short_name) built here would
        // never match the master's remote_idx lookups.
        auto db = CMMakeShared<Database>(result->db_path_, result->data_path_,
                                         worker_id_, data_server_host_, db_path);
        {
            std::unique_lock<std::shared_mutex> db_lk(databases_mutex_);
            databases_[db_path] = db;
        }
        return true;
    }
    return false;
}

CMSharedPtr<Database> WorkerAgent::get_database(const CMString& db_path) const {
    std::shared_lock<std::shared_mutex> db_lk(databases_mutex_);
    auto it = databases_.find(db_path);
    if (it != databases_.end()) {
        return it->second;
    }
    return nullptr;
}

std::pair<CMString, TaskErrorType> WorkerAgent::register_write_with_master(const CMString& db_path, const CMString& object_name, int64_t compressed_size) {
    CMString full_name = db_path + ":" + object_name;
    CMString ctx_hash = fly::WorkerAgentContext::get_current_write_hash();
    if (ctx_hash.empty()) {
        // 未经 commit_write guard / 未设 task context 的路径，用时间戳 fallback，
        // 保证 do_write_register 的 provenance 校验对裸写入也生效。
        ctx_hash = make_timestamp_hash();
    }

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

    WriteRegisterMessage msg;
    msg.worker_id_ = worker_id_;
    msg.object_name_ = full_name;
    msg.db_path_ = db_path;
    msg.write_context_hash_ = ctx_hash;
    msg.size_bytes_ = compressed_size;
    {
        // 锁内只 find + 拷 writer_id（同 request_db_path 的正面范式）——原实现的
        // shared_lock 存活到函数尾，覆盖了 send 和 wait_for 的 5 秒同步等待，
        // 持读锁空等会阻塞任何 register_database（unique）长达 5s。
        std::shared_lock<std::shared_mutex> db_lk(databases_mutex_);
        auto db_it = databases_.find(db_path);
        if (db_it != databases_.end()) {
            msg.writer_id_ = db_it->second->get_writer_id();
        }
    }

    // 未注册窗口（断连重连中，或首连 assign 抢在 RegisterAck 之前）：
    // 写注册直接 pending（用户确认语义）——task 阻塞在此同步点，直到
    // 重连成功且 master 注册确认（Ack → 统一队列重放 → WriteRegisterAck
    // 唤醒），或重连失败/worker 终止（initiate_shutdown 批量 fail 唤醒）。
    // 不放行（数据虽已本地落盘，但 provenance 裁决未下——放行后重放被拒
    // 时错误无法回注已完成的 task）。等待上限为防御值（实际由重连宽限
    // 约束：宽限耗尽 → initiate_shutdown → 批量 fail）。
    if (!registered_.load()) {
        auto pending = CMMakeShared<PendingWriteRegister>();
        pending->object_name_ = full_name;
        pending_write_regs_.emplace(full_name, pending);
        enqueue_master_send([this, msg](uint64_t conn) { reactor_->send(conn, msg); });
        WARN("WriteRegister pending (not registered): object={} — task blocks until "
             "registration confirms or worker exits", full_name);
        auto result = pending_write_regs_.wait_for(full_name, std::chrono::seconds(300),
            [](const CMSharedPtr<PendingWriteRegister>& p) { return p->completed_; });
        pending_write_regs_.erase(full_name);
        if (result && !result->success_) {
            WorkerAgentContext::set_last_error_type(result->error_type_);
            return {result->error_message_, result->error_type_};
        }
        if (!result) {
            // 防御上限耗尽（正常路径不应到达——shutdown/注册确认必先唤醒）。
            // 归类 TIMEOUT：与下方已注册分支的注册超时对称（同一等待语义的
            // 两个窗口），原 UNKNOWN 丢语义。
            CMString error_msg = "WriteRegister pending timeout: " + full_name;
            ERR("{}", error_msg);
            WorkerAgentContext::set_last_error_type(TaskErrorType::WRITE_REGISTRATION_TIMEOUT);
            return {error_msg, TaskErrorType::WRITE_REGISTRATION_TIMEOUT};
        }
        return {"", TaskErrorType::UNKNOWN};
    }

    auto pending = CMMakeShared<PendingWriteRegister>();
    pending->object_name_ = full_name;
    pending_write_regs_.emplace(full_name, pending);

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
    if (!registered_.load()) {
        // A 类挂起（用户确认语义）：VAR_ACK 可携带拒绝，「设置成功」是调用
        // 方语义的一部分。断连窗口不发送，入统一重放队列阻塞等确认。
        enqueue_master_send([this, msg](uint64_t conn) { reactor_->send(conn, msg); });
        WARN("VarSet pending (not registered): var={} — caller blocks until "
             "registration confirms", full_var_name);
        auto result = pending_var_ops_.wait_for(full_var_name, std::chrono::seconds(300),
            [](const CMSharedPtr<PendingVarOp>& p) { return p->completed_; });
        pending_var_ops_.erase(full_var_name);
        if (!result) {
            ERR("VarSet pending timeout: var={}", full_var_name);
            return false;
        }
        return result->success_;
    }
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
    auto pending = CMMakeShared<PendingVarOp>();
    pending->var_name_ = full_var_name;
    pending_var_ops_.emplace(full_var_name, pending);

    VarGetMessage msg;
    msg.var_name_ = full_var_name;  // full name on the wire
    if (!registered_.load()) {
        // A 类挂起：返回值就是 master 上的 var 内容（PendingVarOp::value_
        // 零拷贝回传），天然必须等。断连窗口入队阻塞等确认。
        enqueue_master_send([this, msg](uint64_t conn) { reactor_->send(conn, msg); });
        WARN("VarGet pending (not registered): var={} — caller blocks until "
             "registration confirms", full_var_name);
        auto result = pending_var_ops_.wait_for(full_var_name, std::chrono::seconds(300),
            [](const CMSharedPtr<PendingVarOp>& p) { return p->completed_; });
        pending_var_ops_.erase(full_var_name);
        if (!result) {
            ERR("VarGet pending timeout: var={}", full_var_name);
            return {false, nullptr, ""};
        }
        return {result->success_, result->value_, result->type_name_};
    }
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
    // B 类（原语义 async 无确认）：断连窗口入统一重放队列（注册确认后
    // 重放），不丢删除意图。
    VarRemoveMessage msg;
    msg.var_name_ = full_var_name;  // full name on the wire
    if (!registered_.load()) {
        enqueue_master_send([this, msg](uint64_t conn) { reactor_->send(conn, msg); });
        WARN("VarRemove pending (not registered): var={} — replays after "
             "registration", full_var_name);
        return;
    }
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
    DBG("[SUMMARY] worker {} received MSG_COUNT_REQUEST on conn {}", worker_id_, conn_id);
    MessageCountReportMessage report;
    report.worker_id_ = worker_id_;
    auto id_counts = MessageRegistry::instance().trigger_id_counts_snapshot();
    auto dom_counts = MessageRegistry::instance().trigger_domain_counts_snapshot();
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

void WorkerAgent::on_message_limit_sync(uint64_t /*conn_id*/, const MessageLimitSyncMessage& msg) {
    // 收到 master 的配额全量快照：整体替换本地 Registry 配额。
    // 不清零 trigger/emit 计数——配额改变时，emit 计数保留，按新配额继续判定
    // （调大可继续输出，调小立即受限），trigger 计数持续累加供 summary。
    MessageRegistry::instance().apply_limits_snapshot(
        msg.global_limit_, msg.domain_keys_, msg.domain_values_,
        msg.id_keys_, msg.id_values_);
}

CMVector<VarPayload> WorkerAgent::take_pending_task_vars() {
    std::lock_guard<std::mutex> lk(pending_task_vars_mutex_);
    CMVector<VarPayload> result = std::move(pending_task_vars_);
    pending_task_vars_.clear();
    return result;
}

void WorkerAgent::on_var_ack(uint64_t conn_id, const VarAckMessage& msg) {
    touch_master_contact();

    // 单相 complete：字段写 + notify 全部在 map 锁内。
    // 原两阶段（take_for_complete 锁外写字段 + 无锁 notify_all）有 cv lost
    // wakeup 窗口 + 非 atomic 字段 data race（与 8419526 DataServer::stop 同族），
    // FlyBuffer 构造是纯内存操作（make_shared + move），持锁可接受。
    pending_var_ops_.complete(msg.var_name_, [&](PendingVarOp& p) {
        p.success_ = msg.success_;
        p.error_message_ = msg.error_message_;
        p.type_name_ = msg.type_name_;
        if (msg.success_ && !msg.value_.empty()) {
            // value_ is mutable: std::move it into the FlyBuffer (zero-copy). The
            // decoded ack msg is a local destroyed after this handler returns.
            auto buf = CMMakeShared<FlyBuffer>();
            buf->take(std::move(msg.value_));
            p.value_ = buf;
        }
        p.completed_ = true;
    });
}

void WorkerAgent::on_var_broadcast(uint64_t conn_id, const VarBroadcastMessage& msg) {
    touch_master_contact();

    // msg.var_name_ is a FULL name; split to locate the Database and the short
    // name to drop from its local cache.
    auto [db_path, short_name] = fly::split_full_name(msg.var_name_);
    if (!db_path.empty()) {
        CMSharedPtr<Database> db;
        {
            // 锁内只 find + 拷 shared_ptr（drop_local_var 自保护，D3 拆除）。
            std::shared_lock<std::shared_mutex> db_lk(databases_mutex_);
            auto it = databases_.find(db_path);
            if (it != databases_.end()) {
                db = it->second;
            }
        }
        if (db) {
            db->drop_local_var(short_name);
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

    // B 类：能力视图通知（master 调度匹配用，调用方不依赖确认）。断连窗口
    // 入统一重放队列（add/remove 幂等，重放全部变更不合并）——原静默丢弃
    // 会让 master 能力视图永久陈旧。
    if (!actually_added.empty()) {
        WorkerPropertyUpdateMessage msg;
        msg.worker_id_ = worker_id_;
        msg.added_properties_ = actually_added;
        if (!registered_.load()) {
            enqueue_master_send([this, msg](uint64_t conn) { reactor_->send(conn, msg); });
            WARN("WorkerPropertyUpdate (set) pending (not registered): replays "
                 "after registration");
        } else {
            reactor_->send(master_conn_, msg);
        }

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

    // B 类：同 set 侧——断连窗口入统一重放队列，不丢能力变更。
    if (!actually_removed.empty()) {
        WorkerPropertyUpdateMessage msg;
        msg.worker_id_ = worker_id_;
        msg.removed_properties_ = actually_removed;
        if (!registered_.load()) {
            enqueue_master_send([this, msg](uint64_t conn) { reactor_->send(conn, msg); });
            WARN("WorkerPropertyUpdate (remove) pending (not registered): replays "
                 "after registration");
        } else {
            reactor_->send(master_conn_, msg);
        }

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
    INFO("IdxLoadCommand received: db_path={}, writer_ids_count={}",
         msg.db_path_, msg.writer_ids_.size());

    IdxLoadAckMessage ack;
    ack.worker_id_ = worker_id_;
    ack.db_path_ = msg.db_path_;

    int32_t loaded = 0;
    CMVector<CMString> loaded_writer_ids;
    try {
        auto dsRef = DataService::instance();
        dsRef->register_database(msg.db_path_, "");

        for (const auto& writer_id : msg.writer_ids_) {
            CMString idx_path = msg.db_path_ + "/" + writer_id + ".idx";
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
                dsRef->restore_entries(msg.db_path_, all_entries);
                loaded_writer_ids.push_back(writer_id);
                loaded++;
            }
        }

        ack.success_ = true;
        ack.loaded_count_ = loaded;
        ack.loaded_writer_ids_ = loaded_writer_ids;
        INFO("IdxLoad complete: db_path={}, loaded {} idx files", msg.db_path_, loaded);
    } catch (const std::exception& e) {
        ack.success_ = false;
        ack.error_message_ = e.what();
        ERR("IdxLoad failed: db_path={}, error={}", msg.db_path_, e.what());
    }

    reactor_->send(conn_id, ack);
}

void WorkerAgent::on_storage_spawn_request(uint64_t conn_id, const StorageSpawnRequestMessage& msg) {
    touch_master_contact();
    INFO("StorageSpawnRequest received: spawn_worker_id={}", msg.spawn_worker_id_);

    StorageSpawnAckMessage ack;
    ack.worker_id_ = worker_id_;
    ack.hostname_ = ProcessInfo::instance()->hostname();

    try {
        auto proc = ProcessInfo::instance();
        CMString log_dir = Config::instance()->get_str("log_dir");
        if (log_dir.empty()) log_dir = "fly_log";

        // Config 无 master→worker 同步消息（纯 CLI --config-file 传递），spawn
        // 的子进程同样需要：把当前生效 Config 落盘传递。文件名带 pid——
        // log_dir 全集群共享，不同 host 的并发 spawn 各写各的副本。
        CMString cfg_path = log_dir + "/.fly_config_autospawn_" +
                            std::to_string(static_cast<long long>(getpid()));
        Config::instance()->save_to_file(cfg_path);

        // stdio 兜底（fly 自身 Logger 另有日志文件；这里捕获 crash 输出）。
        CMString stdout_path = log_dir + "/storage_spawn_" +
                               std::to_string(static_cast<long long>(msg.spawn_worker_id_)) + ".log";
        posix_spawn_file_actions_t factions;
        posix_spawn_file_actions_init(&factions);
        posix_spawn_file_actions_addopen(&factions, 0, "/dev/null", O_RDONLY, 0);
        int out_fd = ::open(stdout_path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (out_fd < 0) out_fd = ::open("/dev/null", O_WRONLY);
        posix_spawn_file_actions_adddup2(&factions, out_fd, 1);
        posix_spawn_file_actions_adddup2(&factions, out_fd, 2);
        if (out_fd > 2) posix_spawn_file_actions_addclose(&factions, out_fd);

        // 零继承（storage 是完全独立的进程，exec 后从零初始化，与集群的唯一
        // 协作通道是新建立的 master 连接）：枚举 /proc/self/fd 把发起 worker 的
        // 全部 fd 关进 file_actions——尤其 master 连接 fd。不关的后果实测：
        // 子进程持有连接 fd 副本 → 发起 worker 死后内核不发 FIN → master 永远
        // 看不到断连（连接 ESTAB 残留）→ summary/drain 等待死条目卡死。
        // （glibc 的 POSIX_SPAWN_CLOEXEC_DEFAULT 在本环境 spawn.h 未提供，
        // 逐 fd addclose 是标准接口下的等价实现；spawn 低频，开销可忽略。）
        {
            std::vector<int> inherited_fds;
            DIR* dir = ::opendir("/proc/self/fd");
            if (dir != nullptr) {
                while (struct dirent* ent = ::readdir(dir)) {
                    int fd = ::atoi(ent->d_name);
                    if (fd > 2 && fd != out_fd && fd != dirfd(dir)) {
                        inherited_fds.push_back(fd);
                    }
                }
                ::closedir(dir);
            }
            for (int fd : inherited_fds) {
                posix_spawn_file_actions_addclose(&factions, fd);
            }
        }

        // /proc/self/exe：与发起 worker 严格同二进制（版本零漂移）。
        char exe_buf[64] = "/proc/self/exe";
        int master_port = proc->cli_master_port() != 0 ? proc->cli_master_port()
                                                       : proc->master_port();
        std::string wid_str = std::to_string(msg.spawn_worker_id_);
        std::string port_str = std::to_string(master_port);
        // master 地址用本 worker 实际连接的 master_host()（真实 IP/主机名）；
        // --host 才是逻辑 hostname（含 CLI 覆盖值，单机多 host 场景归属一致）。
        // 两者不能混用：hostname() 是 --host 覆盖后的逻辑名，不可路由。
        std::string master_str(proc->master_host().begin(), proc->master_host().end());
        std::string host_str(proc->hostname().begin(), proc->hostname().end());
        std::string log_dir_str(log_dir.begin(), log_dir.end());
        std::string cfg_str(cfg_path.begin(), cfg_path.end());

        // argv 按 main.cpp 的参数解析构造。
        char* const argv[] = {
            exe_buf,
            const_cast<char*>("--worker"),
            const_cast<char*>("--worker-id"), const_cast<char*>(wid_str.c_str()),
            const_cast<char*>("--worker-role"), const_cast<char*>("storage_only"),
            const_cast<char*>("--master-host"), const_cast<char*>(master_str.c_str()),
            const_cast<char*>("--master-port"), const_cast<char*>(port_str.c_str()),
            const_cast<char*>("--host"), const_cast<char*>(host_str.c_str()),
            const_cast<char*>("--log-dir"), const_cast<char*>(log_dir_str.c_str()),
            const_cast<char*>("--config-file"), const_cast<char*>(cfg_str.c_str()),
            nullptr
        };

        // SETSID 脱离进程树：发起 worker 挂掉时 storage 必须存活（接管价值
        // 所在），其生命周期只由 master 心跳判死管理。fd 零继承见上。
        posix_spawnattr_t attr;
        posix_spawnattr_init(&attr);
        posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSID);

        pid_t pid = -1;
        int rc = posix_spawn(&pid, exe_buf, &factions, &attr, argv, environ);
        posix_spawnattr_destroy(&attr);
        posix_spawn_file_actions_destroy(&factions);
        ::close(out_fd);

        if (rc != 0) {
            ack.success_ = false;
            ack.error_message_ = CMString("posix_spawn failed: ") + std::to_string(rc);
            ERR("storage spawn failed: posix_spawn rc={}", rc);
            reactor_->send(conn_id, ack);
            return;
        }

        // detached 线程回收子进程（storage 是长驻服务，正常不退出；集群
        // drain 时退出——不回收会留 zombie 挂在本 worker 下）。
        std::thread([pid, this]() {
            int status = 0;
            ::waitpid(pid, &status, 0);
            DBG("auto-spawned storage worker pid={} exited (status={})", pid, status);
        }).detach();

        ack.success_ = true;
        INFO("storage worker spawned: pid={}, worker_id={}, host={}, master={}:{}",
             pid, msg.spawn_worker_id_, host_str, master_str, master_port);
    } catch (const std::exception& e) {
        ack.success_ = false;
        ack.error_message_ = e.what();
        ERR("storage spawn failed: {}", e.what());
    }

    reactor_->send(conn_id, ack);
}

void WorkerAgent::on_database_freeze_notification(uint64_t conn_id, const DatabaseFreezeNotification& msg) {
    touch_master_contact();
    INFO("DatabaseFreezeNotification received: db_path={}", msg.db_path_);

    CMSharedPtr<Database> db;
    {
        // 锁内只 find + 拷 shared_ptr：freeze（drain/marker/vars 落盘重 IO）
        // 移出容器锁（D3 拆除）。
        std::shared_lock<std::shared_mutex> db_lk(databases_mutex_);
        auto it = databases_.find(msg.db_path_);
        if (it != databases_.end()) {
            db = it->second;
        }
    }
    if (db) {
        if (db->is_frozen()) {
            INFO("DB already frozen, ignoring broadcast: db_path={}", msg.db_path_);
            return;
        }
        db->freeze();
        INFO("Worker local database frozen: db_path={}", msg.db_path_);
    }
}

void WorkerAgent::on_database_freeze_ack(uint64_t conn_id, const DatabaseFreezeAckMessage& msg) {
    touch_master_contact();
    pending_freezes_.complete(msg.db_path_, [&](PendingFreezeAck& p) {
        p.success_ = msg.success_;
        p.error_type_ = msg.error_type_;
        p.completed_ = true;
    });
}

void WorkerAgent::request_object_remove(const CMString& db_path, const CMString& object_name) {
    CMString full = db_path + ":" + object_name;

    auto pending = CMMakeShared<PendingRemove>();
    pending_removes_.emplace(full, pending);

    RemoveRequestMessage msg;
    msg.db_path_ = db_path;
    msg.object_name_ = full;
    if (!registered_.load()) {
        // A 类挂起（用户确认语义）：删除成败影响后续语义（删除后重写同名
        // 对象的 provenance 链路）。断连窗口不发送，入统一重放队列阻塞等
        // 注册确认后重放拿 RemoveAck。
        enqueue_master_send([this, msg](uint64_t conn) { reactor_->send(conn, msg); });
        WARN("RemoveRequest pending (not registered): object={} — caller blocks "
             "until registration confirms", full);
        auto result = pending_removes_.wait_for(full, std::chrono::seconds(300),
            [](const CMSharedPtr<PendingRemove>& p) { return p->completed_; });
        pending_removes_.erase(full);
        if (!result) {
            ERR("Remove pending timeout: {}", full);
        } else if (!result->success_) {
            ERR("Remove request failed (after replay): {}", full);
        }
        return;
    }

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

    CMSharedPtr<Database> db;
    {
        // 锁内只 find + 拷 shared_ptr：remove_index_entry（idx 文件 IO）
        // 移出容器锁（D3 拆除）。
        std::shared_lock<std::shared_mutex> db_lk(databases_mutex_);
        auto db_it = databases_.find(msg.db_path_);
        if (db_it != databases_.end()) {
            db = db_it->second;
        }
    }
    if (db) {
        CMString short_name = msg.object_name_;
        CMString prefix = msg.db_path_ + ":";
        if (short_name.substr(0, prefix.size()) == prefix) {
            short_name = short_name.substr(prefix.size());
        }
        db->remove_index_entry(short_name);
        INFO("RemoveCommand: persisted REMOVE entry for {}", msg.object_name_);
    }
}

void WorkerAgent::request_backup(const CMString& db_path, const CMString& object_name) {
    CMString full_name = db_path + ":" + object_name;

    BackupRequestMessage msg;
    msg.worker_id_ = worker_id_;
    msg.object_name_ = full_name;
    msg.db_path_ = db_path;
    // B 类：backup 由 master 另派 internal task 异步执行，调用方不依赖结果
    //——断连窗口入统一重放队列（原 `!registered_` 静默 return 会把
    // backup=True 的用户意图永久丢失，对象单副本无兜底）。
    if (!registered_.load()) {
        enqueue_master_send([this, msg](uint64_t conn) { reactor_->send(conn, msg); });
        WARN("BackupRequest pending (not registered): object={} — replays after "
             "registration", full_name);
        return;
    }
    reactor_->send(master_conn_, msg);

    INFO("BackupRequest sent: object={}", full_name);
}

void WorkerAgent::execute_internal_task(const PendingTask& task) {
    if (task.task_name_ == "__backup_object") {
        if (task.args_.size() < 2) {
            ERR("Internal backup task: insufficient args (expected object_name, db_path)");
            TaskFailedMessage failed;
            failed.task_id_ = task.task_id_;
            failed.worker_id_ = worker_id_;
            failed.error_message_ = "Internal backup: insufficient args";
            send_master_or_buffer(failed);
            return;
        }

        CMString object_name = task.args_[0];
        CMString db_path = task.args_[1];

        auto db = get_database(db_path);
        if (!db) {
            if (!request_db_path(db_path)) {
                ERR("Internal backup: failed to get db_path for db_path={}", db_path);
                TaskFailedMessage failed;
                failed.task_id_ = task.task_id_;
                failed.worker_id_ = worker_id_;
                failed.error_message_ = "Internal backup: db_path request failed";
                send_master_or_buffer(failed);
                return;
            }
            db = get_database(db_path);
            if (!db) {
                ERR("Internal backup: still no database for db_path={}", db_path);
                TaskFailedMessage failed;
                failed.task_id_ = task.task_id_;
                failed.worker_id_ = worker_id_;
                failed.error_message_ = "Internal backup: no database";
                send_master_or_buffer(failed);
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
        complete.written_objects_.push_back({db_path + ":" + object_name, 0});
        complete.is_internal_ = true;
        send_master_or_buffer(complete);

        INFO("Internal backup complete: object={}, db_path={}", object_name, db_path);
    } else if (task.task_name_ == "__merge_object") {
        // args: [short_name, source_db_path, target_db_path, target_data_path, source_host]
        // source_db_path = 源 db_path（拉源数据用，源数据登记在此命名空间）
        // target_db_path = merge 产物 db_path（落盘/上报用，master 索引用此 key）
        // target_data_path = master host 本地 data_path（.dat 集中目标）
        if (task.args_.size() < 4) {
            ERR("Internal merge task: insufficient args");
            TaskFailedMessage failed;
            failed.task_id_ = task.task_id_;
            failed.worker_id_ = worker_id_;
            failed.error_message_ = "Internal merge: insufficient args";
            send_master_or_buffer(failed);
            return;
        }
        CMString short_name = task.args_[0];
        CMString source_db_path = task.args_[1];
        CMString target_db_path = task.args_[2];
        CMString target_data_path = task.args_[3];

        execute_merge_object(task.task_id_, short_name, source_db_path, target_db_path, target_data_path);
    } else {
        WARN("Unknown internal task: name={}", task.task_name_);
        TaskFailedMessage failed;
        failed.task_id_ = task.task_id_;
        failed.worker_id_ = worker_id_;
        failed.error_message_ = "Unknown internal task: " + task.task_name_;
        send_master_or_buffer(failed);
    }
}

CMSharedPtr<DataWriter> WorkerAgent::get_or_create_merge_writer(const CMString& db_path,
                                                                 const CMString& target_data_path) {
    // get_or_insert：find 命中复用；miss 时 factory（writer_id 生成 + 构造）在锁内执行。
    // 每个 target_data_path 独占一个 writer_id（merge 专用，避免与源 writer_id 冲突）。
    // idx 写 db_path（共享盘，master 可直读）；.dat 写 target_data_path（master host 本地）。
    return merge_writers_.get_or_insert(target_data_path, [&] {
        CMString merge_writer_id = generate_writer_id();
        int64_t threshold = Config::instance()->get_int("aggregation_threshold");
        auto writer = CMMakeShared<DataWriter>(
            db_path, target_data_path, merge_writer_id, threshold, data_server_host_);
        INFO("Created merge writer: target_data_path={}, writer_id={}", target_data_path, merge_writer_id);
        return writer;
    });
}

void WorkerAgent::execute_merge_object(uint64_t task_id, const CMString& short_name,
                                        const CMString& source_db_path, const CMString& target_db_path,
                                        const CMString& target_data_path) {
    // 拉源用 source_db_path（源数据登记在源命名空间），落盘/上报用 target_db_path（产物命名空间）。
    CMString source_full = source_db_path + ":" + short_name;
    CMString target_full = target_db_path + ":" + short_name;
    INFO("Internal merge: object={}, source={}, target_db_path={}, target_data_path={}",
         short_name, source_db_path, target_db_path, target_data_path);

    auto ds = DataService::instance();

    // 1. 跨机拉源对象压缩字节（用 source_full 查源命名空间的 remote_idx/local_idx）。
    auto [found, comp_data, py_name, source_hash, can_still_produce] =
        ds->read_raw_compressed(source_full);
    if (!found || !comp_data || comp_data->empty()) {
        ERR("Internal merge: no data for '{}'", source_full);
        TaskFailedMessage failed;
        failed.task_id_ = task_id;
        failed.worker_id_ = worker_id_;
        failed.error_message_ = "Internal merge: source object unavailable: " + source_full;
        reactor_->send(master_conn_, failed);
        return;
    }

    // 2. 解析 ObjectHeader 拿到 total_size / chunk_count（落盘需要）。
    //    源数据损坏（header 解析失败）与源缺失同级：TaskFailed，不落盘坏数据。
    int64_t h_off = 0;
    ObjectHeader header;
    if (!ObjectHeader::deserialize(
            CMString(comp_data->data(), comp_data->size()), h_off, header)) {
        ERR("Internal merge: corrupted object header for '{}'", source_full);
        TaskFailedMessage failed;
        failed.task_id_ = task_id;
        failed.worker_id_ = worker_id_;
        failed.error_message_ = "Internal merge: corrupted source object header: " + source_full;
        reactor_->send(master_conn_, failed);
        return;
    }

    // 3. 用 target_db_path 落盘（产物命名空间）。register_database 让本 worker 的 DataServer
    //    能服务 merge 后的对象。
    auto writer = get_or_create_merge_writer(target_db_path, target_data_path);
    ds->register_database(target_db_path, target_data_path, writer->writer_id());

    // 4. 落盘（零解压直写 .dat + idx）。LocalIndex 只存 short_name。
    // write_record_checked/flush_checked/get_last_entry 全链校验：任一失败走
    // TaskFailed——否则 master 会据 TaskComplete 把 remote_idx 指向无数据对象
    //（假成功，读时才炸，merge_db 误判 ok=True 后甚至删源）。
    auto fail_merge_write = [&](const CMString& reason) {
        ds->on_write_failed(target_db_path, target_full, reason);
        TaskFailedMessage failed;
        failed.task_id_ = task_id;
        failed.worker_id_ = worker_id_;
        failed.error_message_ = "Internal merge write failed (" + reason + "): " + short_name;
        reactor_->send(master_conn_, failed);
        ERR("{}", failed.error_message_);
    };
#ifdef FLY_ENABLE_TEST_HOOKS
    // 测试注错：命中集合模拟写盘失败（确定性驱动 TaskFailed 路径）。
    if (merge_write_fail_for_testing_.contains(short_name)) {
        fail_merge_write("injected");
        return;
    }
#endif
    ds->on_write_started(target_db_path, target_full);
    CMString merge_hash = source_hash;
    if (!writer->write_record_checked(short_name, header.total_size_, header.chunk_count_,
                                       *comp_data, merge_hash)) {
        fail_merge_write("write_record");
        return;
    }
    if (!writer->flush_checked()) {
        fail_merge_write("flush");
        return;
    }

    // 5. 登记 local_idx_（target 命名空间）。
    auto last_entry_opt = writer->get_last_entry(short_name);
    if (!last_entry_opt.has_value()) {
        fail_merge_write("idx entry missing after write");
        return;
    }
    CMVector<IndexEntry> new_entries;
    new_entries.push_back(last_entry_opt.value());
    ds->on_write_completed(target_db_path, target_full, new_entries);
    ds->on_object_flushed(target_full);

    // 6. TaskComplete（上报用 target_full，master 用 target 命名空间重建索引）。
    int64_t comp_size = static_cast<int64_t>(comp_data->size());
    TaskCompleteMessage complete;
    complete.task_id_ = task_id;
    complete.worker_id_ = worker_id_;
    complete.written_objects_.push_back({target_full, comp_size});
    complete.is_internal_ = true;
    reactor_->send(master_conn_, complete);

    INFO("Internal merge complete: object={}, target_db_path={}, bytes={}", short_name, target_db_path, comp_size);
}

void WorkerAgent::on_delete_data(uint64_t conn_id, const DeleteDataMessage& msg) {
    touch_master_contact();
    INFO("DeleteData received: db_path={}, data_path={}, writer_ids_count={}",
         msg.db_path_, msg.data_path_, msg.writer_ids_.size());

    DeleteDataAckMessage ack;
    ack.worker_id_ = worker_id_;
    ack.db_path_ = msg.db_path_;

    int32_t deleted = 0;
    CMVector<CMString> deleted_writers;
    try {
        // 直接用消息显式指定的 data_path_（源 data_path），不查 db_registry ——
        // cleanup_after_merge 会把 master 的 db_registry 更新到 merge 路径，若删源在
        // cleanup 之后执行，db_registry 解析会拿到错误的（merge 后的）路径。
        // data_path_ 空时兜底用 db_path_（向后兼容无 data_path_ 的旧调用方）。
        CMString data_dir = msg.data_path_.empty() ? msg.db_path_ : msg.data_path_;

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
        INFO("DeleteData complete: db_path={}, removed {} .dat files from {}",
             msg.db_path_, deleted, data_dir);
    } catch (const std::exception& e) {
        ack.success_ = false;
        ack.error_message_ = e.what();
        ERR("DeleteData failed: db_path={}, error={}", msg.db_path_, e.what());
    }

    reactor_->send(conn_id, ack);
}

// 失败清理：持有该 target merge writer 的 worker 清 local_idx + 删除自己写下的
// 产物文件（.dat + .idx）。文件名规则与 DataWriter 一致：data_{wid}_{idx}.dat
//（data_path 下）、{wid}.idx（db_path 下）。非 merge target worker no-op。
void WorkerAgent::purge_merge_products(const MergeCleanupMessage& msg) {
    auto writer = merge_writers_.take(msg.data_path_);
    if (!writer) {
        return;  // 非 merge target worker（不持有该 target 的 writer）：什么都不动
    }
    DataService::instance()->clear_local_index_for_db(msg.target_db_path_);
    CMSharedPtr<DataWriter> w = *writer;  // optional 解出 shared_ptr
    writer.reset();
    const CMString wid = w->writer_id();
    const int32_t files = w->file_count();
    w.reset();  // 先析构（flush idx），随后连文件一起删

    namespace fs = std::filesystem;
    std::error_code ec;
    for (int32_t i = 1; i <= files; ++i) {
        std::ostringstream oss;
        oss << "data_" << wid << "_" << std::setfill('0') << std::setw(3) << i << ".dat";
        fs::remove(fs::path(msg.data_path_) / oss.str(), ec);
    }
    fs::remove(fs::path(msg.target_db_path_) / (wid + ".idx"), ec);
    INFO("MergeCleanup(purge): removed merge products of writer {} ({} data files) "
         "from target {}", wid, files, msg.data_path_);
}

void WorkerAgent::on_merge_cleanup(uint64_t conn_id, const MergeCleanupMessage& msg) {
    touch_master_contact();
    auto ds = DataService::instance();

    if (msg.purge_target_) {
        // 失败清理模式：源命名空间（db_path_）全保留——源数据支撑重 merge。
        // 只有持有该 target_data_path merge writer 的 worker（即 merge target worker）
        // 才清 local_idx + 删产物；其它 worker（含源 worker）完全 no-op——
        // 同 path merge 时 target_db_path_ == db_path_，无条件清会把源 worker 的
        // 源索引一并清掉（数据在但不可读）。
        purge_merge_products(msg);

        MergeCleanupAckMessage ack;
        ack.worker_id_ = worker_id_;
        ack.db_path_ = msg.db_path_;
        reactor_->send(conn_id, ack);  // master 未登记屏障时 no-op 忽略
        return;
    }

    // 检查本 worker 是否在免清理列表（merge target worker 已持有效 local_idx，跳过清理）。
    bool exempt = false;
    for (uint64_t wid : msg.exempt_worker_ids_) {
        if (wid == worker_id_) { exempt = true; break; }
    }

    if (!exempt) {
        // 1. 清旧索引（源命名空间，指向已删源 .dat / 失效源 worker 位置）。
        //    不碰 ObjectCache（数据内容未变，cache 仍是正确副本）。
        ds->clear_local_index_for_db(msg.db_path_);
        ds->clear_remote_index_for_db(msg.db_path_);
        // cross-path 时也清 target 命名空间（merge worker 用 target 落盘/上报）。
        if (msg.target_db_path_ != msg.db_path_) {
            ds->clear_local_index_for_db(msg.target_db_path_);
            ds->clear_remote_index_for_db(msg.target_db_path_);
        }

        // 2. 注册 target db_path（数据物理位置）。
        ds->register_database(msg.target_db_path_, msg.data_path_, "");

        // 3. load target 目录的新 idx（merge worker 写在 target_db_path_），restore 到 target 命名空间。
        //    若 data_path 可达（同机本地盘或共享 FS），后续读可本地直读 .dat。
        int32_t loaded = 0;
        try {
            namespace fs = std::filesystem;
            if (fs::exists(msg.target_db_path_)) {
                for (const auto& entry : fs::directory_iterator(msg.target_db_path_)) {
                    CMString fname = entry.path().filename().string();
                    if (fname.size() >= 4 &&
                        fname.substr(fname.size() - 4) == ".idx") {
                        LocalIndex idx(entry.path().string());
                        idx.load();
                        auto entries = idx.get_all_entries();
                        if (!entries.empty()) {
                            ds->restore_entries(msg.target_db_path_, entries);
                            ++loaded;
                        }
                    }
                }
            }
        } catch (const std::exception& e) {
            WARN("MergeCleanup: failed to load idx from {}: {}", msg.target_db_path_, e.what());
        }
        INFO("MergeCleanup: db_path={}, target={}, cleared old idx, loaded {} new idx files, "
             "data={} on worker_id={}",
             msg.db_path_, msg.target_db_path_, loaded, msg.data_path_, worker_id_);
    } else {
        INFO("MergeCleanup: worker_id={} exempt (merge target), keeping state for db_path={}",
             worker_id_, msg.db_path_);
    }

    // 无论 exempt 与否，都回 MergeCleanupAck（master 的全局一致性屏障需要收齐所有 worker）。
    MergeCleanupAckMessage ack;
    ack.worker_id_ = worker_id_;
    ack.db_path_ = msg.db_path_;
    reactor_->send(conn_id, ack);
}

// ============================================================
// 业务 RPC（PeerChannelGroup 底层）
// ============================================================

void WorkerAgent::ensure_peer_rpc_handlers() {
    // response_handler（客户端角色：收响应路由到 PendingRpcMap）
    // 线上 status: OK / NOTIFY_FAILURE(全局) / RESPOND_FAILURE(单请求)；
    // 统一映射为内部 PeerRpcStatus：OK → OK, 其他 → ERROR。
    peer_rpc_server_->set_response_handler(
        [this](uint64_t conn_id, uint64_t rpc_id, uint8_t status, const CMString& payload) {
            pending_peer_rpcs_.complete(rpc_id, [&](PendingPeerRpc& p) {
                p.status_.store(static_cast<uint8_t>(
                    status == static_cast<uint8_t>(PeerRpcWireStatus::OK)
                        ? PeerRpcStatus::OK : PeerRpcStatus::ERROR),
                    std::memory_order_release);
                p.payload_ = payload;
            });
        });
    // disconnect_handler：P2P 错误断连时（无 BYE）触发。
    // 1. fail 该连接上所有 pending RPC（compute 的 chan.rpc 立即收到 FAILED）。
    // 2. 入队 error_conns + notify cv（check 的 accept_one 被唤醒并抛异常）。
    peer_rpc_server_->set_disconnect_handler(
        [this](uint64_t conn_id) {
            pending_peer_rpcs_.complete_all_if(
                [conn_id](const PendingPeerRpc& p) { return p.conn_id_ == conn_id; },
                [](PendingPeerRpc& p) {
                    p.status_.store(static_cast<uint8_t>(PeerRpcStatus::FAILED),
                                    std::memory_order_release);
                    p.payload_ = "peer connection closed";
                });
            {
                // notify 持锁防 lost wakeup（同 8419526）。
                std::lock_guard<std::mutex> lk(peer_rpc_incoming_mutex_);
                peer_rpc_error_conns_.push_back(conn_id);
                peer_rpc_incoming_cv_.notify_one();
            }
        });
}

int WorkerAgent::start_peer_rpc_listen(const CMString& host, int port) {
    if (peer_rpc_server_) {
        return peer_rpc_port_;  // 已启动
    }
    peer_rpc_server_ = CMMakeUnique<PeerRpcServer>();
    // 服务端角色：request_handler 收到请求 → 入队（Python while 循环的 recv_request 取出处理）
    int bound_port = peer_rpc_server_->listen(host, port,
        [this](uint64_t conn_id, uint64_t rpc_id, uint64_t src_worker_id,
               const CMString& payload) -> std::optional<CMString> {
            {
                // notify 持锁防 lost wakeup（同 8419526）。
                std::lock_guard<std::mutex> lk(peer_rpc_incoming_mutex_);
                peer_rpc_incoming_.push_back({conn_id, rpc_id, src_worker_id, payload});
                peer_rpc_incoming_cv_.notify_one();
            }
            return std::nullopt;  // 异步处理（Python 层 peer_rpc_respond 回响应）
        });
    ensure_peer_rpc_handlers();
    peer_rpc_port_ = bound_port;  // listen 返回的实际端口
    return peer_rpc_port_;
}

uint64_t WorkerAgent::peer_rpc_connect(const CMString& host, int port,
                                        int retries, int retry_interval_ms) {
    if (!peer_rpc_server_) {
        // 仅客户端模式：创建 PeerRpcServer 但不 listen
        peer_rpc_server_ = CMMakeUnique<PeerRpcServer>();
        ensure_peer_rpc_handlers();
    }
    return peer_rpc_server_->connect_peer(host, port, retries, retry_interval_ms);
}

std::pair<uint8_t, CMString> WorkerAgent::peer_rpc_call(uint64_t conn_id,
                                                         const CMString& payload,
                                                         int timeout_ms) {
    if (!peer_rpc_server_) {
        return {static_cast<uint8_t>(PeerRpcStatus::FAILED), "peer rpc not initialized"};
    }

    uint64_t rpc_id = next_rpc_id_.fetch_add(1, std::memory_order_relaxed);
    auto pending = CMMakeShared<PendingPeerRpc>();
    pending->conn_id_ = conn_id;
    pending_peer_rpcs_.emplace(rpc_id, pending);

    if (!peer_rpc_server_->send_request(conn_id, rpc_id, worker_id_, payload)) {
        pending_peer_rpcs_.erase(rpc_id);
        return {static_cast<uint8_t>(PeerRpcStatus::FAILED), "send_request failed"};
    }

    // timeout_ms <= 0：无限等待（只由响应到达或错误断连唤醒，由 disconnect_handler 保证）。
    auto wait_duration = (timeout_ms > 0)
        ? std::chrono::milliseconds(timeout_ms)
        : std::chrono::hours(24);  // 实际无限，disconnect_handler 保证唤醒
    auto result = pending_peer_rpcs_.wait_for(rpc_id, wait_duration,
        [](const CMSharedPtr<PendingPeerRpc>& p) {
            return p->status_.load(std::memory_order_acquire)
                   != static_cast<uint8_t>(PeerRpcStatus::PENDING);
        });
    pending_peer_rpcs_.erase(rpc_id);

    if (!result) {
        return {static_cast<uint8_t>(PeerRpcStatus::FAILED), "timeout"};
    }
    uint8_t status = result->status_.load(std::memory_order_acquire);
    return {status, std::move(result->payload_)};
}

bool WorkerAgent::peer_rpc_respond(uint64_t conn_id, uint64_t rpc_id,
                                    const CMString& payload) {
    if (!peer_rpc_server_) return false;
    return peer_rpc_server_->send_response(conn_id, rpc_id,
        static_cast<uint8_t>(PeerRpcWireStatus::OK), payload);
}

bool WorkerAgent::peer_rpc_respond_failure(uint64_t conn_id, uint64_t rpc_id,
                                            const CMString& reason) {
    if (!peer_rpc_server_) return false;
    // RESPOND_FAILURE：精确匹配该 rpc_id 的 pending，只 fail 这一个请求。
    // 区别于 notify_failure（NOTIFY_FAILURE, rpc_id=0 全局通知）。
    return peer_rpc_server_->send_response(conn_id, rpc_id,
        static_cast<uint8_t>(PeerRpcWireStatus::RESPOND_FAILURE), reason);
}

WorkerAgent::PeerRpcRequest WorkerAgent::peer_rpc_recv_request(int timeout_ms) {
    std::unique_lock<std::mutex> lk(peer_rpc_incoming_mutex_);
    // 优先检查错误断连（任何 compute 崩溃都意味着收不齐 nsd 个请求）。
    auto check_error = [this]() -> bool {
        if (!peer_rpc_error_conns_.empty()) {
            peer_rpc_error_conns_.clear();
            return true;
        }
        return false;
    };
    if (check_error()) {
        throw std::runtime_error("peer connection error: remote disconnected");
    }
    if (peer_rpc_incoming_.empty()) {
        // timeout_ms <= 0：无限等待，只由请求到达或错误断连唤醒。
        auto pred = [this] {
            return !peer_rpc_incoming_.empty() || !peer_rpc_error_conns_.empty();
        };
        if (timeout_ms <= 0) {
            peer_rpc_incoming_cv_.wait(lk, pred);
        } else {
            peer_rpc_incoming_cv_.wait_for(lk, std::chrono::milliseconds(timeout_ms), pred);
        }
    }
    if (check_error()) {
        throw std::runtime_error("peer connection error: remote disconnected");
    }
    if (peer_rpc_incoming_.empty()) {
        return {0, 0, 0, ""};  // 超时（仅在 timeout_ms > 0 时可能）
    }
    auto req = std::move(peer_rpc_incoming_.front());
    peer_rpc_incoming_.erase(peer_rpc_incoming_.begin());
    return req;
}

bool WorkerAgent::peer_rpc_notify_failure(uint64_t conn_id, const CMString& reason) {
    if (!peer_rpc_server_) return false;
    return peer_rpc_server_->notify_failure(conn_id, reason);
}

void WorkerAgent::peer_rpc_close(uint64_t conn_id) {
    // 优雅关闭：发 BYE → 等服务端 BYE_ACK → close。
    if (peer_rpc_server_) {
        peer_rpc_server_->send_bye(conn_id);
    }
}

void WorkerAgent::stop_peer_rpc() {
    if (peer_rpc_server_) {
        peer_rpc_server_->stop();
        peer_rpc_server_.reset();
        peer_rpc_port_ = 0;
    }
}

}  // namespace fly
