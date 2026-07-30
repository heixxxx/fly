#include <export/cpp/export_macros.h>
#include <message/cpp/message_macros.h>
#include <message/cpp/message_sink.h>
#include <log/cpp/logger.h>
#include <stdexcept>

// 字符串级别 → LogLevel。仅 INFO/WARN/ERROR 合法（DEBUG 不支持 message）。
// 非法级别字符串直接抛错，避免静默降级导致注册了错误类型（review §4.2）。
static fly::LogLevel parse_level(const fly::CMString& s) {
    if (s == "INFO") return fly::LogLevel::INFO;
    if (s == "WARN") return fly::LogLevel::WARN;
    if (s == "ERROR") return fly::LogLevel::ERROR;
    throw std::invalid_argument(
        "invalid message level '" + s + "': must be INFO / WARN / ERROR");
}

// Python 层 MSG 等价逻辑：查 id 绑定级别（未注册丢弃）+ 配额 + 写本地 debug log + push_message。
// 级别由 id 决定，不接收调用方传入的级别。source 为触发位置标识（仅打印标注）。
static void py_message(const fly::CMString& domain_id, int32_t source, const fly::CMString& msg) {
    fly::LogLevel level;
    if (!fly::MessageRegistry::instance().get_level(domain_id, level)) return;  // 未注册丢弃
    if (!fly::MessageRegistry::instance().try_consume(domain_id)) return;
    fly::Logger::instance()->log(level, "[" + domain_id + "] <" + std::to_string(source) + "> " + msg);
    fly::push_message(level, domain_id, source, msg);
}

FLY_EXPORT_MODULE(_fly_message) {

// 注册合法 message id 并绑定级别（白名单）。Python 模块加载时调用。
// level_str: "INFO" / "WARN" / "ERROR"。
FLY_EXPORT_FUNCTION("register_message_id", [](const fly::CMString& domain_id, const fly::CMString& level_str) {
    fly::MessageRegistry::instance().register_id(domain_id, parse_level(level_str));
});

// 设置全局 message id 配额（默认 20；-1 不限；0 禁止）。
FLY_EXPORT_FUNCTION("set_id_limit", [](int32_t limit) {
    fly::MessageRegistry::instance().set_id_limit(limit);
});

// 设置 domain 配额（默认 -1 不限；0 禁止；N 上限）。
FLY_EXPORT_FUNCTION("set_domain_limit", [](const fly::CMString& domain, int32_t limit) {
    fly::MessageRegistry::instance().set_domain_limit(domain, limit);
});

// master 打印配额：控制 worker 推送来的 message 在 master 侧是否打印。
// 独立于各 worker 的触发计数配额（避免 summary 双算）。默认 id=20，domain=-1。
// 仅在 master 进程生效（worker 进程调 MessageSink 无意义但不报错）。
FLY_EXPORT_FUNCTION("set_master_print_id_limit", [](int32_t limit) {
    fly::MessageSink::instance()->set_print_id_limit(limit);
});

FLY_EXPORT_FUNCTION("set_master_print_domain_limit", [](const fly::CMString& domain, int32_t limit) {
    fly::MessageSink::instance()->set_print_domain_limit(domain, limit);
});

// Python 侧 message 发送入口。级别由 id 绑定决定（注册时指定），不接收 level 参数。
// source 为触发位置标识（int，仅打印标注）。
FLY_EXPORT_FUNCTION("send_message", [](const fly::CMString& domain_id, int32_t source, const fly::CMString& msg) {
    py_message(domain_id, source, msg);
});

// master 模式：把 MSG 宏的 push 绑定为 MessageSink 本进程直写。
// 由 fly.runtime 在 master 启动后调用（C++ MasterAgent::start 也会绑定，
// 此处供纯 Python 单进程场景兜底）。
FLY_EXPORT_FUNCTION("set_push_func_local", []() {
    fly::set_message_push_func([](fly::LogLevel level, const fly::CMString& domain_id, int32_t source, const fly::CMString& msg) {
        fly::MessageSink::instance()->handle_local(level, domain_id, source, msg);
    });
});

}
