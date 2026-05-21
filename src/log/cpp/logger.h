#pragma once

#include <common/cpp/common_types.h>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <memory>

namespace fly {

enum class LogLevel : uint8_t {
    DEBUG = 0,
    INFO = 1,
    WARN = 2,
    ERROR = 3,
};

class Logger {
public:
    static Logger& instance();
    static void init(const CMString& base_dir, uint64_t worker_id = 0);
    static CMString resolve_log_dir(const CMString& base_dir);
    static void shutdown();

    void debug(const CMString& msg);
    void info(const CMString& msg);
    void warn(const CMString& msg);
    void error(const CMString& msg);

    void set_level(LogLevel level);
    void flush();

private:
    Logger();
    explicit Logger(const CMString& filename);
    void log(LogLevel level, const CMString& msg);
    CMString level_str(LogLevel level) const;
    CMString timestamp() const;
    static void _update_latest_symlink(const CMString& target_dir, const CMString& base_dir);

    static Logger* instance_;
    CMString filename_;
    std::ofstream file_;
    std::mutex mutex_;
    LogLevel level_;
};

}  // namespace fly

#define DBG(msg)   fly::Logger::instance().debug(msg)
#define INFO(msg)  fly::Logger::instance().info(msg)
#define WARN(msg)  fly::Logger::instance().warn(msg)
#define ERR(msg)   fly::Logger::instance().error(msg)
