#include <export/cpp/export_macros.h>
#include <log/cpp/logger.h>

FLY_EXPORT_MODULE(_fly_log) {

FLY_EXPORT_ENUM(fly::LogLevel, "EXLogLevel")
    FLY_EXPORT_ENUM_VALUE("DEBUG", fly::LogLevel::DEBUG)
    FLY_EXPORT_ENUM_VALUE("INFO", fly::LogLevel::INFO)
    FLY_EXPORT_ENUM_VALUE("WARN", fly::LogLevel::WARN)
    FLY_EXPORT_ENUM_VALUE("ERROR", fly::LogLevel::ERROR);

FLY_EXPORT_FUNCTION("DBG", [](const fly::CMString& msg) {
    fly::Logger::instance()->log(fly::LogLevel::DEBUG, msg);
});

FLY_EXPORT_FUNCTION("INFO", [](const fly::CMString& msg) {
    fly::Logger::instance()->log(fly::LogLevel::INFO, msg);
});

FLY_EXPORT_FUNCTION("WARN", [](const fly::CMString& msg) {
    fly::Logger::instance()->log(fly::LogLevel::WARN, msg);
});

FLY_EXPORT_FUNCTION("ERR", [](const fly::CMString& msg) {
    fly::Logger::instance()->log(fly::LogLevel::ERROR, msg);
});

FLY_EXPORT_FUNCTION("flush_log", []() {
    fly::Logger::instance()->flush();
});

FLY_EXPORT_FUNCTION("init_log", [](const fly::CMString& base_dir, uint64_t worker_id) {
    fly::Logger::init(base_dir, worker_id);
});

FLY_EXPORT_FUNCTION("shutdown_log", []() {
    fly::Logger::shutdown();
});

FLY_EXPORT_FUNCTION("set_log_level", [](fly::LogLevel level) {
    fly::Logger::instance()->set_level(level);
});

}
