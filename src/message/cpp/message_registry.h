#pragma once

#include <common/cpp/common_types.h>
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
// 两层配额（同时生效，任一超限即丢弃，但仍各自 +1 计数）：
//   - id 配额（细，默认 20）：每个 message id 独立计数。
//   - domain 配额（粗，默认 -1=不限）：一个 domain 下所有 id 共享。
// 配额语义：-1 = 不限制；0 = 完全禁止；N = 上限 N 次。
//
// 计数永远累加（不论是否超限丢弃）；未注册的 id 视为非法，直接丢弃且不计次数。
class MessageRegistry {
public:
    static MessageRegistry& instance();

    // 白名单 + 级别：注册合法 id 并绑定其级别（INFO/WARN/ERROR）。
    // 同一 id 重复注册以最后一次为准。DEBUG 不允许（message 不支持 DEBUG 级别）。
    void register_id(const CMString& domain_id, LogLevel level);
    bool is_registered(const CMString& domain_id) const;

    // 查询 id 绑定的级别。未注册返回 false（out 参数不写）。
    bool get_level(const CMString& domain_id, LogLevel& out_level) const;

    // 配额设置。
    // set_id_limit 设置全局 id 配额默认值（对所有 id 生效，默认 20）。
    // set_domain_limit 覆盖单个 domain 的配额（默认 -1=不限，显式设置才生效）。
    void set_id_limit(int32_t limit);
    void set_domain_limit(const CMString& domain, int32_t limit);

    // 核心：尝试消费配额。
    // 无论返回什么，id 计数 +1 且 domain 计数 +1（先累加，再检查配额）。
    // 返回 true = 两层配额都通过（允许打印/发送）；false = 任一层超限（丢弃但仍已计数）。
    // 注意：未注册的 id 由调用方在调本方法前用 is_registered 检查（未注册不计次数）。
    bool try_consume(const CMString& domain_id);

    // 计数快照（summary 上报用）。
    CMUnorderedMap<CMString, uint64_t> id_counts_snapshot() const;
    CMUnorderedMap<CMString, uint64_t> domain_counts_snapshot() const;

    // 从 "DOMAIN::NNNN" 提取 "DOMAIN"。无 "::" 时返回完整串（容错）。
    static CMString extract_domain(const CMString& domain_id);

    // 仅供测试：重置全部状态（白名单 / 计数 / 配额）。
    void reset_for_testing();

private:
    MessageRegistry() = default;

    // 内部配额判断：检查当前累计次数是否仍在配额内（不修改计数）。
    bool id_within_limit(const CMString& domain_id) const;
    bool domain_within_limit(const CMString& domain) const;

    mutable std::mutex mutex_;
    CMUnorderedMap<CMString, LogLevel> registered_ids_;   // id → 绑定级别
    CMUnorderedMap<CMString, uint64_t> id_counts_;        // 每 id 触发次数
    CMUnorderedMap<CMString, uint64_t> domain_counts_;    // 每 domain 触发次数
    int32_t id_limit_ = 20;                                // 全局 id 配额默认值
    CMUnorderedMap<CMString, int32_t> domain_limits_;      // 按 domain 覆盖；未设 = -1
};

}  // namespace fly

