#include <log/cpp/logger.h>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <ctime>
#include <filesystem>

namespace fly {

CMSharedPtr<Logger> Logger::instance() {
    // leak-on-exit（P3-18 根治）：Logger 对象永不析构——退出期（静态析构与
    // Python atexit/coverage 收尾并发）后台线程（ResourceMonitor 等）访问日志
    // 安全。原实现 static shared_ptr 的控制块在静态析构期销毁后，野线程的
    // log_write 拿局部 shared_ptr 做 last-use release → 在已析构控制块上调
    // 虚函数 → "pure virtual method called" → std::terminate（高压 QA 偶发
    // ~1/3000，qa/solver/test_golden_n50_sd4_r30 现场栈定位）。
    // 文件 flush 由显式 shutdown()（各退出路径已调）保证；对象泄漏由进程
    // 退出回收，无实际代价。
    static Logger* inst = new Logger();
    return CMSharedPtr<Logger>(inst, [](Logger*) {});
}

Logger::Logger() : level_(LogLevel::DEBUG), dual_output_(false) {}

void Logger::init(const CMString& base_dir, uint64_t worker_id) {
    CMString dir = _ensure_trailing_sep(base_dir);
    std::filesystem::create_directories(dir);

    CMString log_name = (worker_id == 0) ? "master"
                        : "worker" + std::to_string(worker_id);
    CMString filename = dir + log_name + ".log";

    auto inst = instance();
    std::lock_guard<std::mutex> lock(inst->mutex_);

    if (inst->file_.is_open()) {
        inst->file_.flush();
        inst->file_.close();
    }
    inst->filename_ = filename;
    // dual_output 全部置 false：所有进程的 debug log 只落盘，不再进 terminal。
    // terminal 的唯一输出来源是 message 系统（master 进程的 MessageSink）。
    inst->dual_output_ = false;
    inst->level_ = LogLevel::DEBUG;
    inst->file_.open(inst->filename_, std::ios::out | std::ios::app);
    inst->unflushed_bytes_ = 0;
    inst->last_flush_ = std::chrono::steady_clock::now();
}

// 自动 flush 参数注入（fly_log 不依赖 Config——cc_shared_library 禁止多个 so
// 静态链同一库；由 main.cpp 在 Logger::init 前从 config 读取传入。此后 log
// 调用只读成员，退出期日志不触碰 Config 静态析构，P3-18 同族教训）。
void Logger::set_flush_params(uint64_t threshold_bytes, int64_t interval_ms) {
    auto inst = instance();
    std::lock_guard<std::mutex> lock(inst->mutex_);
    if (threshold_bytes > 0) inst->flush_threshold_bytes_ = threshold_bytes;
    if (interval_ms > 0) inst->flush_interval_ms_ = interval_ms;
}

bool Logger::should_auto_flush() const {
    if (unflushed_bytes_ >= flush_threshold_bytes_) return true;
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - last_flush_).count();
    return elapsed >= flush_interval_ms_;
}

void Logger::note_flush() {
    unflushed_bytes_ = 0;
    last_flush_ = std::chrono::steady_clock::now();
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
    auto inst = instance();
    std::lock_guard<std::mutex> lock(inst->mutex_);
    if (inst->file_.is_open()) {
        inst->file_.flush();
        inst->file_.close();
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
        note_flush();
    }
    if (dual_output_ || !file_.is_open()) {
        std::cerr.flush();
    }
}

void Logger::log(LogLevel level, const CMString& msg) {
    if (level < level_) return;

    std::lock_guard<std::mutex> lock(mutex_);
    CMString line = "[" + timestamp() + "] "
                  + "[" + level_str(level) + "] "
                  + msg + "\n";

    if (file_.is_open()) {
        file_ << line;
        if (level >= LogLevel::WARN) {
            file_.flush();
            note_flush();
        } else {
            // DEBUG/INFO 自动 flush：累计字节达阈值或距上次 flush 超时间间隔
            //（本条触发，写时惰性判定——避免日志文件更新延迟过长）。
            unflushed_bytes_ += line.size();
            if (should_auto_flush()) {
                file_.flush();
                note_flush();
            }
        }
    }

    if (dual_output_ || !file_.is_open()) {
        std::cerr << line;
        if (level >= LogLevel::WARN) {
            std::cerr.flush();
        }
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

    // localtime_r 而非 localtime：TZ 未设置的环境里 glibc localtime 每次调用
    // 触发 tzset_internal(always=1) → stat(/etc/localtime) + 重读解析（每条
    // 日志一次系统调用，strace 实证）；localtime_r 走 always=0 快路径零系统
    // 调用，且线程安全。
    std::tm tm_buf{};
    localtime_r(&time, &tm_buf);
    std::stringstream ss;
    ss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
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
        // remove() only handles files and empty directories. A non-empty
        // directory (left from a previous run with a different layout) would
        // throw; use remove_all to handle that cleanly.
        std::filesystem::remove_all(latest);
    }
    std::filesystem::create_symlink(
        std::filesystem::path(target_dir).filename().string(),
        latest);
}

}  // namespace fly
