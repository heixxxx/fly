#pragma once

#include <container/cpp/container_aliases.h>
#include <log/cpp/logger.h>  // LogLevel
#include <cstdint>
#include <mutex>

namespace fly {

// MessageRegistry — 进程级单例。
//
// message id 格式：字符串 "DOMAIN::NNNN"（domain 大写，id 4 位补零，如 "SOLVER::0047"）。
//
// 级别与 id 绑定：注册时指定该 id 的级别（INFO/WARN/ERROR），发送时不再传级别，
// 由 id 查表决定。未注册的 id 查不到级别 → 丢弃（不计次数）。
//
// 【两套计数模型（关键设计）】
// 一条 message 维护两套独立计数，彻底解耦「触发统计」与「配额判定」：
//   - trigger_count（触发计数）：每次调用 emit 都 +1，无论是否实际输出。
//     永不重置，持续累加。用于 summary 统计「这条 message 被触发了多少次」。
//   - emit_count（输出计数）：仅在实际成功输出（写 log / 推送）时 +1。
//     用于配额判定。配额始终限制的是 emit_count。
// 解耦的好处：动态修改配额时，trigger_count 不影响配额判定。
//   例如 limit=2 触发 100 次（emit_count=2, trigger_count=100），调大 limit=20 后，
//   emit_count=2 < 20 仍可继续输出 18 条——不会因 trigger_count 过大而永远发不出。
//
// 三层配额（链式优先级，仅第一个显式设置的层级生效）：
//   优先级：per-id > per-domain > global，三层都「每 id 独立计数」。
//   - per-id（最细）：set_id_limit(domain_id, limit) 为单个 message id 设独立配额。
//   - per-domain：set_domain_limit(domain, limit) 语义同 global，仅对该 domain 生效。
//   - global（兜底，默认 20）：set_global_limit(limit) 所有未设的 id 的默认配额。
// 配额语义：-1 = 不限制；0 = 完全禁止；N = 上限 N 次（指 emit_count 上限）。
class MessageRegistry {
public:
    static MessageRegistry& instance();

    // 白名单 + 级别：注册合法 id 并绑定其级别（INFO/WARN/ERROR）。
    // 同一 id 重复注册以最后一次为准。DEBUG 不允许（message 不支持 DEBUG 级别）。
    void register_id(const CMString& domain_id, LogLevel level);
    bool is_registered(const CMString& domain_id) const;

    // 查询 id 绑定的级别。未注册返回 false（out 参数不写）。
    bool get_level(const CMString& domain_id, LogLevel& out_level) const;

    // ---- 配额设置（三层链式优先级：per-id > per-domain > global）----
    // global：全局默认配额，对所有未显式设置配额的 id 生效（默认 20）。
    void set_global_limit(int32_t limit);
    // per-id：为单个 message id 设独立配额，覆盖 global 与 domain。
    // 设了 per-id 的 id 只看 per-id 这一层（domain / global 都不检查）。
    void set_id_limit(const CMString& domain_id, int32_t limit);
    // per-domain：为某 domain 下所有未设 per-id 的 id 共享的配额，覆盖 global。
    // 未显式设置的 domain 下沉到 global。
    void set_domain_limit(const CMString& domain, int32_t limit);

    // 核心：记录一次触发 + 判定是否允许输出（写 log / 推送）。
    // 无论返回什么，trigger_count +1（先记录触发）。
    // 配额判定用 emit_count（仅记已成功输出的次数）：
    //   - emit_count < limit → 允许输出，emit_count +1，返回 true。
    //   - emit_count >= limit → 超限丢弃（trigger 已计），返回 false。
    // 注意：未注册的 id 由调用方在调本方法前用 is_registered 检查（未注册不计次数）。
    bool try_emit(const CMString& domain_id);

    // ---- 计数快照（summary 上报用）----
    // trigger 计数（触发次数，进 summary）。
    CMUnorderedMap<CMString, uint64_t> trigger_id_counts_snapshot() const;
    CMUnorderedMap<CMString, uint64_t> trigger_domain_counts_snapshot() const;

    // ---- 配额快照（master → worker 同步用）----
    // 输出当前 global + 所有 domain limits + 所有 id overrides 的快照。
    void get_all_limits(int32_t& global_limit,
                        CMVector<CMString>& domain_keys, CMVector<int32_t>& domain_values,
                        CMVector<CMString>& id_keys, CMVector<int32_t>& id_values) const;
    // 整体替换本地配额（不清零计数，支持运行时动态修改）。
    void apply_limits_snapshot(int32_t global_limit,
                               const CMVector<CMString>& domain_keys, const CMVector<int32_t>& domain_values,
                               const CMVector<CMString>& id_keys, const CMVector<int32_t>& id_values);

    // 从 "DOMAIN::NNNN" 提取 "DOMAIN"。无 "::" 时返回完整串（容错）。
    static CMString extract_domain(const CMString& domain_id);

    // 仅供测试：重置全部状态（白名单 / 计数 / 配额）。
    void reset_for_testing();

private:
    MessageRegistry() = default;

    // 链式优先级选出唯一生效配额（不加锁，调用方持锁）：
    //   per-id 有 → 用 per-id；否则 domain 有 → 用 domain；否则用 global。
    int32_t resolve_effective_limit(const CMString& domain_id, const CMString& domain) const;

    mutable std::mutex mutex_;
    CMUnorderedMap<CMString, LogLevel> registered_ids_;   // id → 绑定级别
    // 两套计数（详见类注释）。
    CMUnorderedMap<CMString, uint64_t> trigger_id_counts_;       // 触发次数（summary用）
    CMUnorderedMap<CMString, uint64_t> trigger_domain_counts_;
    CMUnorderedMap<CMString, uint64_t> emit_id_counts_;          // 输出次数（配额判定用）
    int32_t id_limit_ = 20;                                // global 默认配额
    CMUnorderedMap<CMString, int32_t> id_overrides_;       // per-id 显式配额；未设表示下沉
    CMUnorderedMap<CMString, int32_t> domain_limits_;      // per-domain 显式配额；未设表示下沉
};

}  // namespace fly
