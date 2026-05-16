#include <export/cpp/export_macros.h>
#include <log/cpp/logger.h>
#include <memory>

FLY_EXPORT_MODULE(_fly_log) {

FLY_EXPORT_ENUM(fly::LogLevel, "EXLogLevel")
    FLY_EXPORT_ENUM_VALUE("DEBUG", fly::LogLevel::DEBUG)
    FLY_EXPORT_ENUM_VALUE("INFO", fly::LogLevel::INFO)
    FLY_EXPORT_ENUM_VALUE("WARN", fly::LogLevel::WARN)
    FLY_EXPORT_ENUM_VALUE("ERROR", fly::LogLevel::ERROR);

FLY_EXPORT_CLASS(fly::Logger, "EXLogger")
    FLY_EXPORT_INIT(fly::CMString)
    FLY_EXPORT_METHOD("debug", &fly::Logger::debug)
    FLY_EXPORT_METHOD("info", &fly::Logger::info)
    FLY_EXPORT_METHOD("warn", &fly::Logger::warn)
    FLY_EXPORT_METHOD("error", &fly::Logger::error)
    FLY_EXPORT_METHOD("set_level", &fly::Logger::set_level)
    FLY_EXPORT_METHOD("get_level", &fly::Logger::get_level)
    FLY_EXPORT_METHOD("flush", &fly::Logger::flush);

FLY_EXPORT_FUNCTION("init_master", [](const fly::CMString& path) {
    fly::Logger::init_master(path);
});

FLY_EXPORT_FUNCTION("init_worker", [](uint64_t worker_id, const fly::CMString& path) {
    fly::Logger::init_worker(worker_id, path);
});

FLY_EXPORT_FUNCTION("shutdown", []() {
    fly::Logger::shutdown();
});

FLY_EXPORT_FUNCTION_REF("get_master_log", []() -> fly::Logger& {
    auto* logger = fly::Logger::get_master();
    if (!logger) throw std::runtime_error("Master logger not initialized");
    return *logger;
});

FLY_EXPORT_FUNCTION_REF("get_worker_log", [](uint64_t worker_id) -> fly::Logger& {
    auto* logger = fly::Logger::get_worker(worker_id);
    if (!logger) throw std::runtime_error("Worker logger not initialized");
    return *logger;
});

}