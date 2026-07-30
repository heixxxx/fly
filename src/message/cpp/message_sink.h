#pragma once

#include <common/cpp/common_types.h>
#include <log/cpp/logger.h>  // LogLevel
#include <fstream>
#include <mutex>

namespace fly {

// MessageCounts — 一个进程（master 自身或某 worker）的两套计数快照。
struct MessageCounts {
    CMUnorderedMap<CMString, uint64_t> id_counts_;       // "DOMAIN::NNNN" → 触发次数
    CMUnorderedMap<CMString, uint64_t> domain_counts_;   // "DOMAIN" → 触发次数
};

// MessageSink — 仅 master 进程使用的单例。
//
// 职责：把高价值 message 集中写入 message.log + 输出到 terminal（master terminal
// 唯一来源，因 Logger::dual_output_ 已置 false）。worker 推送来的 message 经
// handle_remote 处理，master 自身经 handle_local 处理。
//
// 进程结束前 print_summary 合并 master 自身 + 各 worker 上报的两套计数，输出汇总。
class MessageSink {
public:
    static CMSharedPtr<MessageSink> instance();

    // 打开 <log_dir>/message.log。由 master 启动时调用。
    void init(const CMString& log_dir);
    void shutdown();

    // master 自身产生的 message（本进程直写，不走网络）。
    // honor_quota=true 时受 master 打印配额控制（与 worker 推送共用，与文档 §3 一致）；
    // honor_quota=false 时豁免配额（系统信息 FLY::0000 等使用，由调用方在更上游已豁免触发计数）。
    // 触发计数由调用方（MSG 宏 / py_message）经 MessageRegistry 记录。
    void handle_local(LogLevel level, const CMString& domain_id, int32_t source,
                      const CMString& msg, bool honor_quota = true);

    // worker 推送来的 message（带来源 worker_id 标注）。
    // 配额检查用 master 独立的 print_counts_（不记触发次数，避免与 worker 双算）。
    // 返回 true=通过配额已打印，false=超限丢弃。
    bool handle_remote(uint64_t worker_id, LogLevel level,
                       const CMString& domain_id, int32_t source, const CMString& msg);

    // 进程结束前调用：合并 master 自身 + 各 worker 的两套计数，打印 summary。
    // reports: 各 worker (worker_id → counts) 的上报；master 自己的 counts 单独传。
    void print_summary(const MessageCounts& master_counts,
                       const CMVector<std::pair<uint64_t, MessageCounts>>& worker_reports);

    // master 打印配额设置（三层链式优先级，与 MessageRegistry 对称：
    // per-id > per-domain > global，仅第一个显式设置的层级生效）。
    // 独立于各 worker 的触发计数配额（避免双算）。语义：-1 = 不限制；0 = 禁止；N = 上限 N 次。
    //   - set_print_global_limit：全局默认打印配额（默认 20）。
    //   - set_print_id_limit(domain_id, limit)：per-id 打印配额，覆盖 global 与 domain。
    //   - set_print_domain_limit(domain, limit)：per-domain 打印配额，覆盖 global。
    void set_print_global_limit(int32_t limit);
    void set_print_id_limit(const CMString& domain_id, int32_t limit);
    void set_print_domain_limit(const CMString& domain, int32_t limit);

    // 构造为 public（参照 Logger），供 CMMakeShared 访问。
    MessageSink() = default;

private:
    void write_line(const CMString& line);  // 加锁写 file_ + stderr
    CMString timestamp() const;
    CMString level_str(LogLevel level) const;

    // master 打印配额：控制 worker 推送来的 message 是否打印（不影响触发计数，
    // 避免与各 worker 的触发计数在 summary 里双算）。链式优先级 per-id > domain > global，
    // 仅取第一个显式设置的层级。
    bool print_within_limit(const CMString& domain_id);

    CMString filename_;
    std::ofstream file_;
    std::mutex mutex_;
    bool inited_ = false;

    int32_t print_id_limit_ = 20;                            // global 默认打印配额
    CMUnorderedMap<CMString, int32_t> print_id_overrides_;   // per-id 显式；未设 = 下沉
    CMUnorderedMap<CMString, int32_t> print_domain_limits_;  // per-domain 显式；未设 = 下沉
    CMUnorderedMap<CMString, uint64_t> print_id_counts_;     // 已打印次数
    CMUnorderedMap<CMString, uint64_t> print_domain_counts_;
};

}  // namespace fly
