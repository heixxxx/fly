#pragma once

#include <container/cpp/container_aliases.h>
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

// ---- fatal message 分发（MSG_FATAL_EXIT 的第 4 步，进程退出前最后动作）----
// 与 push_message / set_message_push_func 完全同构的指针注入（message 模块
// 不依赖 network/agent——分发目标由各进程启动时绑定）：
//   - worker：绑定为 WorkerAgent::send_fatal_to_master（发送 master + 等写
//     缓冲排空，有界超时；本地 debug log 已落盘，超时不阻塞退出）。
//   - master：绑定为 MessageSink 写行（message.log + terminal，豁免配额）+
//     detached 线程 fast_exit → _exit(同码)。
//   - 单测 / 非 agent 进程：默认 nullptr（仅本地 debug log，随后 _exit）。
using FatalDispatchFunc = std::function<void(const CMString& domain_id, int32_t source,
                                             int32_t exit_code, const CMString& msg)>;
void set_fatal_dispatch_func(FatalDispatchFunc func);

// fatal message 退出路径（[[noreturn]]，MSG_FATAL_EXIT 宏与 Python
// fly.fatal_message 共用的唯一实现）。流程（顺序即红线：落盘 → 分发 → 退出）：
//   1. id 未注册 → WARN 提示（与 MSG 一致）后跳过配额/级别查表（仍退出）。
//   2. 豁免 emit 配额（record_trigger_only：仍记 trigger 计数进 summary）。
//   3. Logger::log(FATAL) 写本地 debug log（FATAL 与 WARN/ERROR 同走立即
//      flush 路径）+ 显式 Logger::flush() 双保险。
//   4. fatal 分发（set_fatal_dispatch_func 注入；未绑定则跳过）。
//   5. _exit(exit_code)：跳过静态析构——日志与 sink 已在前面显式 flush，
//      数据安全；静态析构期的 WBQ/网络线程拆除不在 fatal 语义内。
[[noreturn]] void fatal_exit(const CMString& domain_id, int32_t source,
                             int32_t exit_code, const CMString& msg);

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

// 发出 fatal message 后以错误码退出程序（不返回）。适用于**不可恢复的**
// 数据/结构损坏（层级树多根/环、权威段损坏、存储校验损坏等）——进程级
// fatal 语义，非编程错误守卫（后者仍用 throw / assert）。
//
// 用法：MSG_FATAL_EXIT("STOR::0005", 0, 80, "object '{}' corrupt: {}", name, detail);
//   - domain_id: message id（"DOMAIN::NNNN"，注册级别应为 FATAL）。
//   - source: 触发位置标识（业务自定义，仅打印标注）。
//   - exit_code: 进程退出码（fly 全局统一 80，避开 77=std::terminate / 78=signal）。
//   - master 侧联动：worker 的 fatal 经 FATAL_MESSAGE 送达 master → master
//     fast_exit（失败在途任务 + StopNow 杀全部 worker）→ master _exit(同码)。
//
// 流程详见 fly::fatal_exit 注释（未注册 WARN 后仍退出 / 豁免配额记 trigger /
// 本地落盘立即 flush / 分发 / _exit）。宏不返回，调用点后续代码不可达。
#define MSG_FATAL_EXIT(domain_id, source, exit_code, fmt_str, ...) \
    do { \
        ::fly::fatal_exit((domain_id), (source), (exit_code), \
            ::fly::format_log(FMT_STRING(fmt_str), ##__VA_ARGS__)); \
    } while (0)

