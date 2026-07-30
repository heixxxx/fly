#include <message/cpp/message_registry.h>

namespace fly {

MessageRegistry& MessageRegistry::instance() {
    static MessageRegistry inst;
    return inst;
}

void MessageRegistry::register_id(const CMString& domain_id, LogLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    registered_ids_[domain_id] = level;
}

bool MessageRegistry::is_registered(const CMString& domain_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return registered_ids_.find(domain_id) != registered_ids_.end();
}

bool MessageRegistry::get_level(const CMString& domain_id, LogLevel& out_level) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = registered_ids_.find(domain_id);
    if (it == registered_ids_.end()) return false;
    out_level = it->second;
    return true;
}

void MessageRegistry::set_id_limit(int32_t limit) {
    std::lock_guard<std::mutex> lock(mutex_);
    id_limit_ = limit;
}

void MessageRegistry::set_domain_limit(const CMString& domain, int32_t limit) {
    std::lock_guard<std::mutex> lock(mutex_);
    domain_limits_[domain] = limit;
}

bool MessageRegistry::id_within_limit(const CMString& domain_id) const {
    // -1 = 不限制；0 = 禁止（try_consume 先 +1 再判断，故 count>=1 时 1<=0 false 必然超限）。
    // count 在 try_consume 里已 +1，故用 count <= limit 判定「第 count 次是否仍允许」。
    if (id_limit_ < 0) return true;
    auto it = id_counts_.find(domain_id);
    uint64_t count = (it != id_counts_.end()) ? it->second : 0;
    return count <= static_cast<uint64_t>(id_limit_);
}

bool MessageRegistry::domain_within_limit(const CMString& domain) const {
    auto it = domain_limits_.find(domain);
    int32_t limit = (it != domain_limits_.end()) ? it->second : -1;  // 默认 -1=不限
    if (limit < 0) return true;
    auto cit = domain_counts_.find(domain);
    uint64_t count = (cit != domain_counts_.end()) ? cit->second : 0;
    return count <= static_cast<uint64_t>(limit);
}

bool MessageRegistry::try_consume(const CMString& domain_id) {
    CMString domain = extract_domain(domain_id);

    std::lock_guard<std::mutex> lock(mutex_);
    // 先累加两套计数（无论是否超限丢弃，触发次数都 +1）。
    id_counts_[domain_id]++;
    domain_counts_[domain]++;

    // 再检查两层配额（同时生效，任一超限即丢弃）。
    return id_within_limit(domain_id) && domain_within_limit(domain);
}

CMUnorderedMap<CMString, uint64_t> MessageRegistry::id_counts_snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return id_counts_;
}

CMUnorderedMap<CMString, uint64_t> MessageRegistry::domain_counts_snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return domain_counts_;
}

CMString MessageRegistry::extract_domain(const CMString& domain_id) {
    auto pos = domain_id.find("::");
    if (pos == CMString::npos) return domain_id;
    return domain_id.substr(0, pos);
}

void MessageRegistry::reset_for_testing() {
    std::lock_guard<std::mutex> lock(mutex_);
    registered_ids_.clear();
    id_counts_.clear();
    domain_counts_.clear();
    id_limit_ = 20;
    domain_limits_.clear();
}

}  // namespace fly
