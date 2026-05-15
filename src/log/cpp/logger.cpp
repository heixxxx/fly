#include <log/cpp/logger.h>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <filesystem>

namespace fly {

CMMap<CMString, std::unique_ptr<Logger>> Logger::instances_;
std::mutex Logger::instance_mutex_;
CMString Logger::log_path_ = "logs/";

Logger::Logger(const CMString& filename)
    : filename_(filename), level_(LogLevel::DEBUG) {
    file_.open(filename_, std::ios::out | std::ios::app);
}

Logger::~Logger() {
    if (file_.is_open()) {
        file_.close();
    }
}

void Logger::debug(const CMString& component, const CMString& msg) {
    log(LogLevel::DEBUG, component, msg);
}

void Logger::info(const CMString& component, const CMString& msg) {
    log(LogLevel::INFO, component, msg);
}

void Logger::warn(const CMString& component, const CMString& msg) {
    log(LogLevel::WARN, component, msg);
}

void Logger::error(const CMString& component, const CMString& msg) {
    log(LogLevel::ERROR, component, msg);
}

void Logger::set_level(LogLevel level) {
    level_ = level;
}

LogLevel Logger::get_level() const {
    return level_;
}

void Logger::flush() {
    if (file_.is_open()) {
        file_.flush();
    }
}

void Logger::log(LogLevel level, const CMString& component, const CMString& msg) {
    if (level < level_) return;
    
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_.is_open()) {
        file_ << "[" << timestamp() << "] "
              << "[" << level_str(level) << "] "
              << "[" << component << "] "
              << msg << std::endl;
    }
}

CMString Logger::level_str(LogLevel level) const {
    switch (level) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO";
        case LogLevel::WARN:  return "WARN";
        case LogLevel::ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

CMString Logger::timestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
    ss << "." << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

Logger* Logger::get_master() {
    std::lock_guard<std::mutex> lock(instance_mutex_);
    auto it = instances_.find("master");
    if (it != instances_.end()) {
        return it->second.get();
    }
    return nullptr;
}

Logger* Logger::get_worker(uint64_t worker_id) {
    std::lock_guard<std::mutex> lock(instance_mutex_);
    CMString name = "worker" + std::to_string(worker_id);
    auto it = instances_.find(name);
    if (it != instances_.end()) {
        return it->second.get();
    }
    return nullptr;
}

void Logger::init_master(const CMString& path) {
    std::lock_guard<std::mutex> lock(instance_mutex_);
    log_path_ = path;
    
    std::filesystem::create_directories(log_path_);
    
    CMString filename = log_path_ + "master.log";
    instances_["master"] = std::make_unique<Logger>(filename);
}

void Logger::init_worker(uint64_t worker_id, const CMString& path) {
    std::lock_guard<std::mutex> lock(instance_mutex_);
    log_path_ = path;
    
    std::filesystem::create_directories(log_path_);
    
    CMString name = "worker" + std::to_string(worker_id);
    CMString filename = log_path_ + name + ".log";
    instances_[name] = std::make_unique<Logger>(filename);
}

void Logger::shutdown() {
    std::lock_guard<std::mutex> lock(instance_mutex_);
    for (auto& [name, logger] : instances_) {
        if (logger) {
            logger->flush();
        }
    }
    instances_.clear();
}

}  // namespace fly