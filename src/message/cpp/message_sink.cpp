#include <message/cpp/message_sink.h>
#include <message/cpp/message_registry.h>  // extract_domain
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <ctime>
#include <filesystem>

namespace fly {

CMSharedPtr<MessageSink> MessageSink::instance() {
    static CMSharedPtr<MessageSink> inst = CMMakeShared<MessageSink>();
    return inst;
}

void MessageSink::init(const CMString& log_dir) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (inited_) {
        if (file_.is_open()) file_.flush();
        return;
    }
    // log_dir 已含 trailing sep（由 Logger::resolve_log_dir / _ensure_trailing_sep 保证）。
    CMString dir = log_dir;
    if (!dir.empty() && dir.back() != '/') dir += '/';
    filename_ = dir + "message.log";
    std::filesystem::create_directories(dir);
    file_.open(filename_, std::ios::out | std::ios::app);
    inited_ = true;
}

void MessageSink::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_.is_open()) {
        file_.flush();
        file_.close();
    }
    inited_ = false;
}

CMString MessageSink::timestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    struct tm tm_local;
    localtime_r(&time, &tm_local);  // 可重入，多线程安全（timestamp 在 mutex 外被调用）
    std::stringstream ss;
    ss << std::put_time(&tm_local, "%Y-%m-%d %H:%M:%S");
    ss << "." << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

CMString MessageSink::level_str(LogLevel level) const {
    switch (level) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO";
        case LogLevel::WARN:  return "WARN";
        case LogLevel::ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

void MessageSink::write_line(const CMString& line) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_.is_open()) {
        file_ << line;
        file_.flush();  // message 是高价值信息，立即落盘
    }
    // terminal 输出（master 进程 stderr 是终端唯一来源，debug log 已不进 terminal）
    std::cerr << line;
    std::cerr.flush();
}

void MessageSink::handle_local(LogLevel level, const CMString& domain_id, int32_t source,
                               const CMString& msg, bool honor_quota) {
    // honor_quota=true：master 自身 message 受 master 打印配额控制（与 worker 推送共用，
    // 与文档 §3 承诺一致）；honor_quota=false：豁免（FLY::0000 系统信息用）。
    if (honor_quota && !print_within_limit(domain_id)) {
        return;  // 超限丢弃
    }
    CMString line = "[" + timestamp() + "] [" + level_str(level) +
                    "] [master] [" + domain_id + "] <" + std::to_string(source) + "> " + msg + "\n";
    write_line(line);
}

void MessageSink::set_print_global_limit(int32_t limit) {
    std::lock_guard<std::mutex> lock(mutex_);
    print_id_limit_ = limit;
}

void MessageSink::set_print_id_limit(const CMString& domain_id, int32_t limit) {
    std::lock_guard<std::mutex> lock(mutex_);
    print_id_overrides_[domain_id] = limit;
}

void MessageSink::set_print_domain_limit(const CMString& domain, int32_t limit) {
    std::lock_guard<std::mutex> lock(mutex_);
    print_domain_limits_[domain] = limit;
}

bool MessageSink::print_within_limit(const CMString& domain_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    CMString domain = MessageRegistry::extract_domain(domain_id);

    // 链式优先级选出唯一生效配额（per-id > domain > global），三层语义统一为「每 id 独立计数」。
    auto id_it = print_id_overrides_.find(domain_id);
    int32_t limit;
    if (id_it != print_id_overrides_.end()) {
        limit = id_it->second;
    } else {
        auto dom_it = print_domain_limits_.find(domain);
        limit = (dom_it != print_domain_limits_.end()) ? dom_it->second : print_id_limit_;
    }

    // 配额判定用「已打印计数」emit 语义：先判，通过才 +1（与 MessageRegistry::try_emit 对齐）。
    // 这样动态调大配额时不会因历史打印数过大而永远打不出（emit 计数只记已成功打印的）。
    if (limit >= 0 && print_id_counts_[domain_id] >= static_cast<uint64_t>(limit)) {
        return false;  // 超限丢弃
    }
    print_id_counts_[domain_id]++;
    print_domain_counts_[domain]++;
    return true;
}

bool MessageSink::handle_remote(uint64_t worker_id, LogLevel level,
                                const CMString& domain_id, int32_t source, const CMString& msg) {
    // master 打印配额：控制 worker 推送来的 message 是否在 master 侧打印。
    // 不记触发次数（触发发生在 worker，已由 worker 的 MessageRegistry 记录）。
    if (!print_within_limit(domain_id)) {
        return false;  // 超限丢弃
    }
    CMString line = "[" + timestamp() + "] [" + level_str(level) +
                    "] [worker" + std::to_string(worker_id) + "] [" + domain_id +
                    "] <" + std::to_string(source) + "> " + msg + "\n";
    write_line(line);
    return true;
}

void MessageSink::print_summary(const MessageCounts& master_counts,
                                const CMVector<std::pair<uint64_t, MessageCounts>>& worker_reports) {
    // 合并：master 自身 + 所有 worker 的两套计数。
    CMUnorderedMap<CMString, uint64_t> total_id;
    CMUnorderedMap<CMString, uint64_t> total_domain;

    auto merge = [&](const MessageCounts& c) {
        for (const auto& [k, v] : c.id_counts_) total_id[k] += v;
        for (const auto& [k, v] : c.domain_counts_) total_domain[k] += v;
    };
    merge(master_counts);
    for (const auto& [wid, counts] : worker_reports) {
        (void)wid;
        merge(counts);
    }

    // 构造 summary 文本块。
    std::stringstream ss;
    ss << "\n========== Message Trigger Summary ==========\n";

    ss << "--- By message id ---\n";
    if (total_id.empty()) {
        ss << "  (no message triggered)\n";
    } else {
        // 按 domain 分组、id 排序输出，便于阅读。
        // 收集 (domain, id) 对并排序。
        CMVector<std::pair<CMString, CMString>> entries;
        for (const auto& [k, v] : total_id) {
            entries.push_back(std::make_pair(MessageRegistry::extract_domain(k), k));
        }
        std::sort(entries.begin(), entries.end());
        for (const auto& [domain, full_id] : entries) {
            ss << "  " << full_id << " : " << total_id[full_id] << "\n";
        }
    }

    ss << "--- By domain ---\n";
    if (total_domain.empty()) {
        ss << "  (no message triggered)\n";
    } else {
        CMVector<std::pair<CMString, uint64_t>> dom_entries(
            total_domain.begin(), total_domain.end());
        std::sort(dom_entries.begin(), dom_entries.end());
        for (const auto& [domain, cnt] : dom_entries) {
            ss << "  " << domain << " : " << cnt << "\n";
        }
    }
    ss << "=============================================\n";

    CMString summary = ss.str();
    // summary 同时写 message.log 和 terminal。
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_.is_open()) {
        file_ << summary;
        file_.flush();
    }
    std::cerr << summary;
    std::cerr.flush();
}

}  // namespace fly
