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
    explicit Logger(const CMString& filename);
    ~Logger();
    
    void debug(const CMString& component, const CMString& msg);
    void info(const CMString& component, const CMString& msg);
    void warn(const CMString& component, const CMString& msg);
    void error(const CMString& component, const CMString& msg);
    
    void set_level(LogLevel level);
    LogLevel get_level() const;
    
    void flush();
    
    static Logger* get_master();
    static Logger* get_worker(uint64_t worker_id);
    static void init_master(const CMString& path = "logs/");
    static void init_worker(uint64_t worker_id, const CMString& path = "logs/");
    static void shutdown();
    
private:
    CMString filename_;
    std::ofstream file_;
    std::mutex mutex_;
    LogLevel level_;
    
    void log(LogLevel level, const CMString& component, const CMString& msg);
    CMString level_str(LogLevel level) const;
    CMString timestamp() const;
    
    static CMMap<CMString, std::unique_ptr<Logger>> instances_;
    static std::mutex instance_mutex_;
    static CMString log_path_;
};

}  // namespace fly