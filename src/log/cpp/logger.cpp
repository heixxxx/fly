#include <log/cpp/logger.h>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <filesystem>

namespace fly {

Logger* Logger::instance_ = nullptr;

Logger::Logger() : level_(LogLevel::DEBUG) {}

Logger::Logger(const CMString& filename)
    : filename_(filename), level_(LogLevel::DEBUG) {
    if (!filename_.empty()) {
        file_.open(filename_, std::ios::out | std::ios::app);
    }
}

Logger& Logger::instance() {
    static Logger fallback;
    if (instance_) {
        return *instance_;
    }
    return fallback;
}

void Logger::init(const CMString& base_dir, uint64_t worker_id) {
    CMString dir = _ensure_trailing_sep(base_dir);
    std::filesystem::create_directories(dir);

    CMString log_name = (worker_id == 0) ? "master"
                        : "worker" + std::to_string(worker_id);
    CMString filename = dir + log_name + ".log";

    if (instance_) {
        instance_->flush();
        delete instance_;
    }
    instance_ = new Logger(filename);
}

CMString Logger::resolve_log_dir(const CMString& base_dir) {
    namespace fs = std::filesystem;

    if (!fs::exists(base_dir)) {
        fs::create_directories(base_dir);
        _update_latest_symlink(base_dir, base_dir);
        return _ensure_trailing_sep(base_dir);
    }

    uint32_t highest = 0;
    while (fs::exists(base_dir + "." + std::to_string(highest + 1))) {
        highest++;
    }

    CMString target = base_dir + "." + std::to_string(highest + 1);
    fs::create_directories(target);
    _update_latest_symlink(target, base_dir);
    return _ensure_trailing_sep(target);
}

void Logger::shutdown() {
    if (instance_) {
        instance_->flush();
        delete instance_;
        instance_ = nullptr;
    }
}

void Logger::vlog(LogLevel level, fmt::string_view fmt, fmt::format_args args) {
    if (level < level_) return;
    log(level, fmt::vformat(fmt, args));
}

void Logger::set_level(LogLevel level) { level_ = level; }

void Logger::flush() {
    if (file_.is_open()) {
        file_.flush();
    }
}

void Logger::log(LogLevel level, const CMString& msg) {
    if (level < level_) return;

    std::lock_guard<std::mutex> lock(mutex_);
    if (file_.is_open()) {
        file_ << "[" << timestamp() << "] "
              << "[" << level_str(level) << "] "
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

CMString Logger::_ensure_trailing_sep(const CMString& path) {
    if (path.empty() || path.back() == '/') return path;
    return path + '/';
}

void Logger::_update_latest_symlink(const CMString& target_dir, const CMString& base_dir) {
    CMString latest = base_dir + ".latest";
    if (std::filesystem::is_symlink(latest)) {
        std::filesystem::remove(latest);
    } else if (std::filesystem::exists(latest)) {
        std::filesystem::remove(latest);
    }
    std::filesystem::create_symlink(
        std::filesystem::path(target_dir).filename().string(),
        latest);
}

}  // namespace fly
