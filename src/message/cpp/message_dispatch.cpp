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

// ---- fatal message 分发（槽位与退出路径，详见 message_macros.h 声明处注释）----
namespace {
FatalDispatchFunc& fatal_dispatch_slot() {
    static FatalDispatchFunc func;
    return func;
}
}  // namespace

void set_fatal_dispatch_func(FatalDispatchFunc func) {
    fatal_dispatch_slot() = std::move(func);
}

void fatal_exit(const CMString& domain_id, int32_t source, int32_t exit_code, const CMString& msg) {
    // 1. id 未注册 → WARN 提示（与 MSG 宏一致；未注册是编程错误，必须可见）。
    //    未注册仅跳过级别查表与分发前置校验——不改变退出语义；计数同样跳过
    //    （与 try_emit 的既有约定一致：未注册不计次数，见 message_registry.h）。
    LogLevel level = LogLevel::FATAL;
    const bool registered = MessageRegistry::instance().get_level(domain_id, level);
    if (!registered) {
        Logger::instance()->log(LogLevel::WARN,
            "[MSG] unregistered message id '" + domain_id +
            "' in fatal path — register it via MessageRegistry::register_id before use");
        level = LogLevel::FATAL;
    }
    // 2. 豁免 emit 配额：必然输出，但触发次数照记（summary 可见）。
    if (registered) {
        MessageRegistry::instance().record_trigger_only(domain_id);
    }
    // 3. 本地 debug log 落盘（FATAL 走 WARN/ERROR 同款立即 flush 路径）+
    //    显式 flush 双保险。此步完成后日志数据已安全，后续任何路径不丢。
    CMString prefix = "[" + domain_id + "] <" + std::to_string(source) + "> ";
    Logger::instance()->log(level, prefix + msg);
    Logger::instance()->flush();
    // 4. fatal 分发：worker = 发送 master 并等写缓冲排空；master = sink 落盘
    //    terminal + fast_exit。未绑定（单测/非 agent 进程）→ 跳过。
    const auto& func = fatal_dispatch_slot();
    if (func) {
        func(domain_id, source, exit_code, msg);
    }
    // 分发过程中新产生的日志（如 worker 送达失败的 WARN——虽走立即 flush 路径，
    // 此处统一收口不依赖各路径 flush 行为）也必须落盘后再退出。
    Logger::instance()->flush();
    // 5. _exit：跳过静态析构（日志已显式 flush，数据安全）。
    ::_exit(exit_code);
}

}  // namespace fly
