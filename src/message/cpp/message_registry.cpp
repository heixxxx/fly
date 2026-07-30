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

void MessageRegistry::set_global_limit(int32_t limit) {
    std::lock_guard<std::mutex> lock(mutex_);
    id_limit_ = limit;
}

void MessageRegistry::set_id_limit(const CMString& domain_id, int32_t limit) {
    std::lock_guard<std::mutex> lock(mutex_);
    id_overrides_[domain_id] = limit;
}

void MessageRegistry::set_domain_limit(const CMString& domain, int32_t limit) {
    std::lock_guard<std::mutex> lock(mutex_);
    domain_limits_[domain] = limit;
}

int32_t MessageRegistry::resolve_effective_limit(const CMString& domain_id,
                                                 const CMString& domain) const {
    // 链式优先级：per-id > per-domain > global。仅取第一个「显式设置」的层级。
    auto id_it = id_overrides_.find(domain_id);
    if (id_it != id_overrides_.end()) return id_it->second;
    auto dom_it = domain_limits_.find(domain);
    if (dom_it != domain_limits_.end()) return dom_it->second;
    return id_limit_;
}

bool MessageRegistry::try_emit(const CMString& domain_id) {
    CMString domain = extract_domain(domain_id);

    std::lock_guard<std::mutex> lock(mutex_);
    // 1. 记录触发（无论是否输出，trigger_count 都 +1；进 summary）。
    trigger_id_counts_[domain_id]++;
    trigger_domain_counts_[domain]++;

    // 2. 配额判定用 emit_count（仅记已成功输出的次数）。
    int32_t limit = resolve_effective_limit(domain_id, domain);
    if (limit >= 0 && emit_id_counts_[domain_id] >= static_cast<uint64_t>(limit)) {
        return false;  // 超限丢弃（trigger 已计，emit 不增）
    }
    // 3. 允许输出，emit_count +1。
    emit_id_counts_[domain_id]++;
    return true;
}

CMUnorderedMap<CMString, uint64_t> MessageRegistry::trigger_id_counts_snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return trigger_id_counts_;
}

CMUnorderedMap<CMString, uint64_t> MessageRegistry::trigger_domain_counts_snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return trigger_domain_counts_;
}

void MessageRegistry::get_all_limits(int32_t& global_limit,
                                     CMVector<CMString>& domain_keys, CMVector<int32_t>& domain_values,
                                     CMVector<CMString>& id_keys, CMVector<int32_t>& id_values) const {
    std::lock_guard<std::mutex> lock(mutex_);
    global_limit = id_limit_;
    domain_keys.clear();
    domain_values.clear();
    for (const auto& [k, v] : domain_limits_) {
        domain_keys.push_back(k);
        domain_values.push_back(v);
    }
    id_keys.clear();
    id_values.clear();
    for (const auto& [k, v] : id_overrides_) {
        id_keys.push_back(k);
        id_values.push_back(v);
    }
}

void MessageRegistry::apply_limits_snapshot(int32_t global_limit,
                                            const CMVector<CMString>& domain_keys, const CMVector<int32_t>& domain_values,
                                            const CMVector<CMString>& id_keys, const CMVector<int32_t>& id_values) {
    std::lock_guard<std::mutex> lock(mutex_);
    // 整体替换配额（不清零 trigger/emit 计数，支持运行时动态修改）。
    id_limit_ = global_limit;
    domain_limits_.clear();
    for (size_t i = 0; i < domain_keys.size() && i < domain_values.size(); ++i) {
        domain_limits_[domain_keys[i]] = domain_values[i];
    }
    id_overrides_.clear();
    for (size_t i = 0; i < id_keys.size() && i < id_values.size(); ++i) {
        id_overrides_[id_keys[i]] = id_values[i];
    }
}

CMString MessageRegistry::extract_domain(const CMString& domain_id) {
    auto pos = domain_id.find("::");
    if (pos == CMString::npos) return domain_id;
    return domain_id.substr(0, pos);
}

void MessageRegistry::reset_for_testing() {
    std::lock_guard<std::mutex> lock(mutex_);
    registered_ids_.clear();
    trigger_id_counts_.clear();
    trigger_domain_counts_.clear();
    emit_id_counts_.clear();
    id_limit_ = 20;
    id_overrides_.clear();
    domain_limits_.clear();
}

}  // namespace fly
