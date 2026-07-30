#include <message/cpp/message_macros.h>
#include <log/cpp/logger.h>

namespace fly {

// 进程级全局推送函数指针。各进程启动时设置：
//   - worker：由 WorkerAgent::start 绑定为发送到 master（经 reactor）。
//   - master：由 MasterAgent::start 绑定为 MessageSink::handle_local。
//   - 单测 / 非 agent：默认 nullptr（push_message no-op）。
namespace {
MessagePushFunc& push_func_slot() {
    static MessagePushFunc func;
    return func;
}
}  // namespace

void set_message_push_func(MessagePushFunc func) {
    push_func_slot() = std::move(func);
}

void push_message(LogLevel level, const CMString& domain_id, int32_t source, const CMString& msg) {
    const auto& func = push_func_slot();
    if (func) {
        func(level, domain_id, source, msg);
    }
}

// system sink 槽位：仅 master 绑定（MessageSink::handle_local），worker 不绑定。
namespace {
SystemSinkFunc& system_sink_slot() {
    static SystemSinkFunc func;
    return func;
}
}  // namespace

void set_system_sink_func(SystemSinkFunc func) {
    system_sink_slot() = std::move(func);
}

void emit_system_message(LogLevel level, const CMString& domain_id, int32_t source, const CMString& msg) {
    // 豁免配额：所有进程都写本地 debug log（带 [domain_id] <source> 前缀）。
    Logger::instance()->log(level, "[" + domain_id + "] <" + std::to_string(source) + "> " + msg);
    // 若 system sink 已绑定（master）→ 走 MessageSink（message.log + terminal）。
    // worker 不绑定 → 仅本地 debug log，不发送 master。
    const auto& sink = system_sink_slot();
    if (sink) {
        sink(level, domain_id, source, msg);
    }
}

// ---- 配额变更回调 ----
// master 进程绑定此回调为 broadcast_message_limits（把配额同步给所有 worker）。
// worker 进程 / 单测不绑定 → no-op。
namespace {
LimitChangeCallback& limit_change_slot() {
    static LimitChangeCallback cb;
    return cb;
}
}  // namespace

void set_limit_change_callback(LimitChangeCallback cb) {
    limit_change_slot() = std::move(cb);
}

void notify_limit_changed() {
    const auto& cb = limit_change_slot();
    if (cb) {
        cb();
    }
}

}  // namespace fly
