#include <core/cpp/system_info.h>
#include <core/cpp/config.h>
#include <core/cpp/process_info.h>
#include <build_info/build_info.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <chrono>
#include <ctime>
#include <cstdio>

namespace fly {

CMString SystemInfo::align(const CMString& label, const CMString& value, size_t width) {
    CMString padded = label;
    if (padded.size() < width) padded.append(width - padded.size(), ' ');
    return "  " + padded + " : " + value + "\n";
}

// 读取首个匹配行的值部分（去掉 "key:" 前缀）。
static CMString read_proc_first(const CMString& path, const CMString& prefix) {
    std::ifstream ifs(path);
    CMString line;
    while (std::getline(ifs, line)) {
        if (line.compare(0, prefix.size(), prefix) == 0) {
            auto pos = line.find(':');
            if (pos != CMString::npos) {
                CMString v = line.substr(pos + 1);
                size_t start = v.find_first_not_of(" \t");
                return (start == CMString::npos) ? v : v.substr(start);
            }
        }
    }
    return "unknown";
}

// CPU 型号。
static CMString cpu_model() {
    return read_proc_first("/proc/cpuinfo", "model name");
}

// CPU 核心数。
static CMString cpu_cores() {
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return std::to_string(n);
}

// 从 /proc/meminfo 读指定字段的 kB 数值（返回字节）。
static uint64_t meminfo_bytes(const CMString& key) {
    CMString v = read_proc_first("/proc/meminfo", key);
    // 形如 "6073204 kB"，取首段数字按 kB 解析。
    std::istringstream iss(v);
    uint64_t kb = 0;
    iss >> kb;
    return kb * 1024ull;
}

// 字节 → GB 数值字符串（保留 1 位小数，不含单位）。
static CMString gb_num(uint64_t bytes) {
    double gb = static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f", gb);
    return buf;
}

// 内存信息：free / available / total（数值用 / 分隔，单位 GB 仅在末尾）。
static CMString memory_info() {
    uint64_t total = meminfo_bytes("MemTotal");
    uint64_t free = meminfo_bytes("MemFree");
    uint64_t avail = meminfo_bytes("MemAvailable");
    if (total == 0) return "unknown";
    return gb_num(free) + "/" + gb_num(avail) + "/" + gb_num(total) + " GB";
}

// 当前进程物理内存占用（VmRSS，GB），括号内为物理峰值（VmHWM）。
// 2026-08-16 改动：此前显示 VmPeak（虚拟地址空间峰值）——多线程 C++ 的
// 线程栈/malloc arena 虚拟预留使其常态上 GB，与实际物理占用无关，诊断
// 内存压力时误导（push OOM 排查中被误读为 1.2GB 占用）。
static CMString process_memory() {
    CMString rss_v = read_proc_first("/proc/self/status", "VmRSS");
    CMString hwm_v = read_proc_first("/proc/self/status", "VmHWM");
    std::istringstream rss_iss(rss_v);
    std::istringstream hwm_iss(hwm_v);
    uint64_t rss_kb = 0, hwm_kb = 0;
    rss_iss >> rss_kb;
    hwm_iss >> hwm_kb;
    if (rss_kb == 0) return "unknown";
    CMString s = gb_num(rss_kb * 1024ull) + " GB";
    if (hwm_kb > 0) s += " (peak " + gb_num(hwm_kb * 1024ull) + " GB)";
    return s;
}

// 磁盘：available / total（单位 GB 仅在末尾）。
static CMString disk_total() {
    namespace fs = std::filesystem;
    std::error_code ec;
    auto info = fs::space(fs::current_path(), ec);
    if (ec) return "unknown";
    return gb_num(info.available) + "/" + gb_num(info.capacity) + " GB";
}

// 当前用户名。
static CMString current_user() {
    const char* user = std::getenv("USER");
    if (!user) user = std::getenv("LOGNAME");
    if (!user) {
        user = "unknown";
    }
    return user;
}

// OS 信息（uname）。
static CMString os_info() {
    struct utsname u;
    if (uname(&u) != 0) return "unknown";
    return CMString(u.sysname) + " " + u.release + " " + u.machine;
}

// 本机 IP（取非 loopback 的首个 IPv4，找不到回退 127.0.0.1）。
static CMString local_ip() {
    // 简单方式：解析 /proc/net/fib_trie 不通用；用 hostname -I 不稳。
    // 这里读 gethostname 后 getaddrinfo 解析，取首个非 127 的 IPv4。
    char host[256] = {};
    if (gethostname(host, sizeof(host)) != 0) return "127.0.0.1";
    // 优先返回 ProcessInfo 里的 master_host 或 data_server_host 作展示。
    return ProcessInfo::instance()->data_server_host();
}

// 当前时间格式化（可重入 localtime_r）。
static CMString now_str() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    struct tm tm_local;
    localtime_r(&t, &tm_local);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_local);
    return buf;
}

// binary 绝对路径。
static CMString binary_path() {
    char buf[4096] = {};
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len > 0) {
        buf[len] = '\0';
        return buf;
    }
    return "unknown";
}

// message.log 路径（与 master.log 同目录）。
static CMString message_log_path() {
    CMString log_dir = Config::instance()->get_str("log_dir");
    return log_dir + "message.log";
}

CMString SystemInfo::format_startup_info(const CMString& role, int listening_port) {
    std::ostringstream oss;
    const size_t w = 12;  // 字段名对齐宽度（紧凑）

    oss << "========== Fly Startup Info (" << role << ") ==========\n";

    // --- Binary ---
    oss << "--- Binary ---\n";
    oss << align("binary", binary_path(), w);
    oss << align("build", CMString(build_info::kBuildType) + " @ " + build_info::kBuildTime
                              + " (commit " + build_info::kGitCommit + ")", w);

    // --- Machine ---
    oss << "--- Machine ---\n";
    oss << align("host", current_user() + "@" + ProcessInfo::instance()->hostname(), w);
    oss << align("os", os_info(), w);
    oss << align("cpu", cpu_model() + " (" + cpu_cores() + " cores)", w);
    oss << align("pid", std::to_string(getpid()), w);
    oss << align("memory", memory_info(), w);
    oss << align("proc mem", process_memory(), w);
    oss << align("disk", disk_total(), w);

    // --- Network ---
    oss << "--- Network ---\n";
    int port = listening_port;
    if (port == 0) port = ProcessInfo::instance()->master_port();
    oss << align("listen", local_ip() + ":" + std::to_string(port), w);

    // --- Runtime ---
    oss << "--- Runtime ---\n";
    oss << align("log", Config::instance()->get_str("log_dir"), w);
    oss << align("msg log", message_log_path(), w);
    oss << align("script", ProcessInfo::instance()->script_path().empty()
                                ? CMString("(none)")
                                : ProcessInfo::instance()->script_path(), w);
    oss << align("started", now_str(), w);

    return oss.str();
}

}  // namespace fly
