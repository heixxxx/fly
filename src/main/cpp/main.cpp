#include <Python.h>
#include <core/cpp/config.h>
#include <core/cpp/process_info.h>
#include <log/cpp/logger.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <filesystem>
#include <exception>
#include <execinfo.h>
#include <signal.h>
#include <fstream>
#include <thread>
#include <atomic>
#include <unistd.h>
#include <sstream>

static void crash_backtrace() {
    void* buf[64];
    int n = backtrace(buf, 64);
    backtrace_symbols_fd(buf, n, STDERR_FILENO);
}

static std::string get_mem_info() {
    std::ifstream ifs("/proc/self/status");
    std::string line, result;
    while (std::getline(ifs, line)) {
        if (line.compare(0, 6, "VmRSS:") == 0 ||
            line.compare(0, 7, "VmSize:") == 0 ||
            line.compare(0, 8, "VmPeak:") == 0) {
            result += line + " ";
        }
    }
    return result;
}

static std::atomic<bool> g_monitor_running{false};

static void resource_monitor_loop() {
    long hz = sysconf(_SC_CLK_TCK);
    long prev_utime = 0, prev_stime = 0;
    long prev_total_cpu = 0;
    auto prev_wall = std::chrono::steady_clock::now();

    {
        std::ifstream ifs("/proc/self/stat");
        if (ifs) {
            std::string line;
            std::getline(ifs, line);
            std::istringstream iss(line);
            std::string tok;
            int idx = 0;
            while (iss >> tok) {
                if (idx == 13) prev_utime = std::stol(tok);
                if (idx == 14) prev_stime = std::stol(tok);
                ++idx;
            }
        }
        std::ifstream ifs2("/proc/stat");
        if (ifs2) {
            std::string line;
            std::getline(ifs2, line);
            std::istringstream iss(line.substr(5));
            std::string tok;
            while (iss >> tok) {
                if (!tok.empty()) prev_total_cpu += std::stol(tok);
            }
        }
    }

    while (g_monitor_running.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        if (!g_monitor_running.load()) break;

        long utime = 0, stime = 0;
        std::ifstream ifs("/proc/self/stat");
        if (ifs) {
            std::string line;
            std::getline(ifs, line);
            std::istringstream iss(line);
            std::string tok;
            int idx = 0;
            while (iss >> tok) {
                if (idx == 13) utime = std::stol(tok);
                if (idx == 14) stime = std::stol(tok);
                ++idx;
            }
        }

        long total_cpu = 0;
        std::ifstream ifs2("/proc/stat");
        if (ifs2) {
            std::string line;
            std::getline(ifs2, line);
            std::istringstream iss(line.substr(5));
            std::string tok;
            while (iss >> tok) {
                if (!tok.empty()) total_cpu += std::stol(tok);
            }
        }

        long rss_kb = 0;
        std::ifstream ifs3("/proc/self/status");
        if (ifs3) {
            std::string line;
            while (std::getline(ifs3, line)) {
                if (line.compare(0, 6, "VmRSS:") == 0) {
                    rss_kb = std::stol(line.substr(6));
                    break;
                }
            }
        }

        auto now = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(now - prev_wall).count();
        double dproc = (utime - prev_utime + stime - prev_stime) / (double)hz;
        double dtot = (total_cpu - prev_total_cpu) / (double)hz;
        double cpu_pct = dtot > 0.001 ? dproc / dtot * 100.0 : 0.0;

        DBG("ResourceMonitor cpu={:.1f}% rss={}MB", cpu_pct, rss_kb / 1024);

        prev_utime = utime;
        prev_stime = stime;
        prev_total_cpu = total_cpu;
        prev_wall = now;
    }
}

static void terminate_handler() {
    fprintf(stderr, "\n=== FATAL: std::terminate ===\n");
    auto ex = std::current_exception();
    if (ex) {
        try {
            std::rethrow_exception(ex);
        } catch (const std::bad_alloc& e) {
            fprintf(stderr, "Exception: std::bad_alloc [%s]  %s\n",
                    e.what(), get_mem_info().c_str());
        } catch (const std::exception& e) {
            fprintf(stderr, "Exception: %s [%s]\n",
                    typeid(e).name(), e.what());
        } catch (...) {
            fprintf(stderr, "Exception: unknown\n");
        }
    }
    crash_backtrace();
    _exit(77);
}

static void sig_handler(int sig) {
    fprintf(stderr, "\n=== FATAL: signal %d ===\n", sig);
    crash_backtrace();
    _exit(78);
}

static void setup_sys_path() {
    // Determine layout: build/ (installed) vs bazel-bin/ (legacy)
    std::string ps = "import sys, os\n";

    // Priority 1: FLY_BUILD environment variable
    const char* fly_build_env = std::getenv("FLY_BUILD");
    // Priority 2: Derive from binary path (e.g., /root/fly/build/bin/fly → /root/fly/build)
    std::filesystem::path build_dir;
    bool use_build_layout = false;

    if (fly_build_env && std::string(fly_build_env) != "") {
        build_dir = std::filesystem::path(fly_build_env);
        use_build_layout = true;
    } else {
        // Derive from binary path
        char exe_path[4096];
        ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
        if (len > 0) {
            exe_path[len] = '\0';
            std::filesystem::path exe_dir = std::filesystem::path(exe_path).parent_path();
            // Try build/ layout first (exe_dir is build/bin)
            std::filesystem::path candidate = exe_dir.parent_path();
            if (std::filesystem::exists(candidate / "lib") && std::filesystem::exists(candidate / "python")) {
                build_dir = candidate;
                use_build_layout = true;
            } else {
                // Try bazel-bin/ layout (exe_dir is bazel-bin/src/main/cpp)
                std::filesystem::path project_root = exe_dir.parent_path().parent_path().parent_path().parent_path();
                if (std::filesystem::exists(project_root / "bazel-bin")) {
                    build_dir = project_root / "bazel-bin";
                }
            }
        }
    }

    if (use_build_layout) {
        // build/ layout: modules are under build/python/<module>/
        // fly package source is under build/python/fly/
        auto py_dir = build_dir / "python";
        ps += "sys.path.insert(0, '" + (py_dir / "core").string() + "')\n";
        ps += "sys.path.insert(0, '" + (py_dir / "storage").string() + "')\n";
        ps += "sys.path.insert(0, '" + (py_dir / "agent").string() + "')\n";
        ps += "sys.path.insert(0, '" + (py_dir / "log").string() + "')\n";
        ps += "sys.path.insert(0, '" + (py_dir / "network").string() + "')\n";
        ps += "sys.path.insert(0, '" + (py_dir / "task").string() + "')\n";
        ps += "sys.path.insert(0, '" + (py_dir / "test").string() + "')\n";
        ps += "sys.path.insert(0, '" + (py_dir / "solver").string() + "')\n";
        ps += "sys.path.insert(0, '" + py_dir.string() + "')\n";
        // Third-party packages (cloudpickle/numpy/scipy) copied by fly.sh install
        // from bazel @pip wheels. Added after fly's own modules so production fly
        // is hermetic — it imports them from here, not the system site-packages.
        // Falls through silently if absent (e.g. dev builds without install) so
        // the binary still runs when third-party deps aren't bundled.
        auto third_party_site = py_dir / "lib" / "python3.12" / "site-packages";
        if (std::filesystem::exists(third_party_site)) {
            ps += "sys.path.insert(0, '" + third_party_site.string() + "')\n";
        }
        // Expose the wrapper script path so Python (get_fly_binary) can spawn
        // fly subprocesses without guessing. The wrapper sets FLY_BUILD and
        // LD_LIBRARY_PATH, so it must be preferred over the raw fly.bin.
        ps += "sys._fly_binary = '" + (build_dir / "bin" / "fly").string() + "'\n";
    } else if (!build_dir.empty()) {
        // bazel-bin/ layout (for Bazel test targets)
        ps += "sys.path.insert(0, '" + (build_dir / "src" / "core" / "export").string() + "')\n";
        ps += "sys.path.insert(0, '" + (build_dir / "src" / "storage" / "export").string() + "')\n";
        ps += "sys.path.insert(0, '" + (build_dir / "src" / "agent" / "export").string() + "')\n";
        ps += "sys.path.insert(0, '" + (build_dir / "src" / "log" / "export").string() + "')\n";
        ps += "sys.path.insert(0, '" + (build_dir / "src" / "network" / "export").string() + "')\n";
        ps += "sys.path.insert(0, '" + (build_dir / "src" / "task" / "export").string() + "')\n";
        ps += "sys.path.insert(0, '" + (build_dir / "src" / "test" / "export").string() + "')\n";
        ps += "sys.path.insert(0, '" + (build_dir / "src" / "solver" / "export").string() + "')\n";
        auto project_root = build_dir.parent_path();
        ps += "sys.path.insert(0, '" + (project_root / "src").string() + "')\n";
        ps += "sys._fly_binary = '" + (build_dir / "src" / "main" / "cpp" / "fly").string() + "'\n";
    }

    ps += "import _fly_core\n";
    ps += "import _fly_log\n";
    ps += "import _fly_storage\n";
    ps += "import _fly_agent\n";
    ps += "import _fly_task\n";
    ps += "import _fly_test\n";
    ps += "import _fly_solver\n";

    PyRun_SimpleString(ps.c_str());
}

static void print_usage(const char* prog) {
    printf("Usage: %s [options] [script]\n", prog);
    printf("Options:\n");
    printf("  --worker             Run in worker mode\n");
    printf("  --worker-id N        Worker ID (default: 0)\n");
    printf("  --master-host HOST   Master host (default: 127.0.0.1)\n");
    printf("  --master-port PORT   Master port (default: 0)\n");
    printf("  --log-dir DIR        Log directory (default: fly_log)\n");
    printf("  --host HOST          Host override for registration\n");
    printf("  -i                   Interactive mode\n");
    printf("  script               Python script to execute\n");
}

int main(int argc, char* argv[]) {
    std::set_terminate(terminate_handler);
    signal(SIGABRT, sig_handler);
    signal(SIGSEGV, sig_handler);

    bool worker_mode = false;
    int worker_id = 0;
    std::string master_host = "127.0.0.1";
    int master_port = 0;
    std::string log_dir = "fly_log";
    bool interactive = false;
    std::string script_path;
    std::string worker_attributes;
    std::string host_override;
    std::string config_file;

    for (int i = 1; i < argc; i++) {
        std::string arg(argv[i]);
        if (arg == "--worker") {
            worker_mode = true;
        } else if (arg == "--worker-id" && i + 1 < argc) {
            worker_id = std::stoi(argv[++i]);
        } else if (arg == "--master-host" && i + 1 < argc) {
            master_host = argv[++i];
        } else if (arg == "--master-port" && i + 1 < argc) {
            master_port = std::stoi(argv[++i]);
        } else if (arg == "--log-dir" && i + 1 < argc) {
            log_dir = argv[++i];
        } else if (arg == "--worker-attributes" && i + 1 < argc) {
            worker_attributes = argv[++i];
        } else if (arg == "--host" && i + 1 < argc) {
            host_override = argv[++i];
        } else if (arg == "--config-file" && i + 1 < argc) {
            config_file = argv[++i];
        } else if (arg == "-i") {
            interactive = true;
        } else if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        } else if (!arg.empty() && arg[0] != '-') {
            script_path = arg;
        }
    }

    auto cfg = Config::instance();
    auto proc = ProcessInfo::instance();

    if (!config_file.empty()) {
        cfg->load_from_file(config_file);
    }

    proc->set_worker_mode(worker_mode);
    proc->set_worker_id(worker_id);
    proc->set_master_host(master_host);
    proc->set_master_port(master_port);
    proc->set_cli_master_port(master_port);
    cfg->set_str("log_dir", log_dir);
    proc->set_interactive(interactive);
    proc->set_script_path(script_path);
    proc->set_worker_attributes(worker_attributes);

    if (!host_override.empty()) {
        proc->set_hostname(host_override);
    }

    if (!worker_mode) {
        log_dir = fly::Logger::resolve_log_dir(log_dir);
        cfg->set_str("log_dir", log_dir);
    }
    fly::Logger::init(log_dir, worker_id);

    g_monitor_running.store(true);
    std::thread monitor_thread(resource_monitor_loop);

    wchar_t** wargv = new wchar_t*[argc];
    for (int i = 0; i < argc; ++i) {
        wargv[i] = Py_DecodeLocale(argv[i], nullptr);
    }

    Py_Initialize();
    PySys_SetArgv(argc, wargv);
    setup_sys_path();

    int rc = PyRun_SimpleString(
        "import sys\n"
        "import fly.bootstrap\n"
        "from fly.main import run\n"
        "sys.exit(run())\n"
    );

    g_monitor_running.store(false);
    monitor_thread.join();

    fly::Logger::shutdown();

    Py_Finalize();

    for (int i = 0; i < argc; ++i) {
        PyMem_RawFree(wargv[i]);
    }
    delete[] wargv;

    return rc != 0 ? 1 : 0;
}
