#pragma once

#include <common/cpp/common_types.h>
#include <log/cpp/logger.h>
#include <message/cpp/message_registry.h>
#include <functional>

namespace fly {

// message 推送目标。
// worker 进程：绑定为「发送到 master」（经 WorkerAgentContext 桥接）。
// master 进程：绑定为 MessageSink::handle_local（本进程直写 message.log + terminal）。
// 单测 / 非 agent 进程：默认为 nullptr（message 仅写本地 debug log，不推送）。
// 参数：level, domain_id, source（触发位置标识，仅打印标注）, msg。
using MessagePushFunc = std::function<void(LogLevel, const CMString&, int32_t, const CMString&)>;

void set_message_push_func(MessagePushFunc func);

// push_message：在 message 通过配额检查后调用，把 message 路由到 master（worker）
// 或 MessageSink（master）。若 push func 未设置则 no-op（仅本地 debug log 已写）。
void push_message(LogLevel level, const CMString& domain_id, int32_t source, const CMString& msg);

// ---- 系统 message（FLY::0000 等，豁免配额）----
// 用于启动信息等重要的基础信息：豁免 message id/domain 两层配额，必定打印。
// master：写本地 debug log + MessageSink（message.log + terminal）。
// worker：仅写本地 debug log，不发送 master（system_sink 未绑定）。
using SystemSinkFunc = std::function<void(LogLevel, const CMString&, int32_t, const CMString&)>;

// 绑定 system sink（仅 master 进程绑定为 MessageSink::handle_local）。
// worker 进程不绑定（emit_system_message 时 sink 为空 → 仅本地 debug log）。
void set_system_sink_func(SystemSinkFunc func);

// emit_system_message：豁免配额，写本地 debug log + 若 system sink 已绑定则走 sink。
// domain_id 通常为 "FLY::0000"。source 用于多行信息的行号区分。
void emit_system_message(LogLevel level, const CMString& domain_id, int32_t source, const CMString& msg);

// ---- 配额变更回调（master → worker 同步触发点）----
// 用户调 set_*_limit 后触发。master 进程绑定此回调为「广播配额给所有 worker」，
// worker 进程 / 单测不绑定（回调为空时 no-op）。详见 docs/message-system.md §10。
using LimitChangeCallback = std::function<void()>;
void set_limit_change_callback(LimitChangeCallback cb);
void notify_limit_changed();

}  // namespace fly

// --- MSG 宏 ---
//
// 用法：MSG("SOLVER::0047", 3, "收敛于 {}", residual);
//   - domain_id: message id（"DOMAIN::NNNN"），级别由注册时绑定决定。
//   - source: int，触发位置标识（业务自定义），打印为 [DOMAIN::NNNN] <source> msg，
//             用于同一 id 在不同位置触发时快速定位。不参与配额。
//
// 逻辑：
//   1. 查 id 绑定的级别；未注册 → WARN 提示后丢弃（用户裁定：未注册的信息
//      不可默认丢弃且无提示——与配额超限的设计性静默不同，未注册是编程
//      错误，必须可见）。WARN 通道立即 flush，保证提示不丢。
//   2. try_emit（trigger 计数 +1；配额判定用 emit 计数，详见 MessageRegistry）。
//      - 任一层超限 → 丢弃（不写 debug log，不推送；次数已计）。
//      - 两层通过 → 用 id 绑定的级别写本地 debug log（带 [DOMAIN::NNNN] <source> 前缀）+ push_message 推送。
//
// 与 DBG/INFO/WARN/ERR 完全独立：前者不动。message 复用 Logger::log 写本地 debug log，
// 保证研发可见；额外经 push_message 触发远程推送或 master 本地落盘。
//
// 格式化委托给 fly::format_log（logger.h），MSG 宏本身不直接依赖 fmt，避免重复链接。
#define MSG(domain_id, source, fmt_str, ...) \
    do { \
        const ::fly::CMString& _msg_domain = (domain_id); \
        ::fly::LogLevel _msg_level; \
        if (::fly::MessageRegistry::instance().get_level(_msg_domain, _msg_level)) { \
            if (::fly::MessageRegistry::instance().try_emit(_msg_domain)) { \
                ::fly::CMString _msg_text = ::fly::format_log(FMT_STRING(fmt_str), ##__VA_ARGS__); \
                ::fly::CMString _msg_prefix = "[" + _msg_domain + "] <" + std::to_string(source) + "> "; \
                ::fly::Logger::instance()->log(_msg_level, _msg_prefix + _msg_text); \
                ::fly::push_message(_msg_level, _msg_domain, source, _msg_text); \
            } \
        } else { \
            ::fly::Logger::instance()->log(::fly::LogLevel::WARN, \
                "[MSG] unregistered message id '" + _msg_domain + \
                "' dropped — register it via MessageRegistry::register_id before use"); \
        } \
    } while (0)

