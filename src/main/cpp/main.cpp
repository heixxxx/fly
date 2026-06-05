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
    std::filesystem::path cwd = std::filesystem::current_path();

    // Determine layout: build/ (installed) vs bazel-bin/ (legacy)
    std::string ps = "import sys, os\n";

    const char* fly_build_env = std::getenv("FLY_BUILD");
    std::filesystem::path build_dir;
    bool use_build_layout = false;

    if (fly_build_env && std::string(fly_build_env) != "") {
        build_dir = std::filesystem::path(fly_build_env);
        use_build_layout = true;
    } else if (std::filesystem::exists(cwd / "build" / "bin" / "fly")) {
        build_dir = cwd / "build";
        use_build_layout = true;
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
    } else {
        // Fallback: bazel-bin/ layout (for Bazel test targets)
        std::filesystem::path bazel_bin = cwd / "bazel-bin";
        ps += "sys.path.insert(0, '" + (bazel_bin / "src" / "core" / "export").string() + "')\n";
        ps += "sys.path.insert(0, '" + (bazel_bin / "src" / "storage" / "export").string() + "')\n";
        ps += "sys.path.insert(0, '" + (bazel_bin / "src" / "agent" / "export").string() + "')\n";
        ps += "sys.path.insert(0, '" + (bazel_bin / "src" / "log" / "export").string() + "')\n";
        ps += "sys.path.insert(0, '" + (bazel_bin / "src" / "network" / "export").string() + "')\n";
        ps += "sys.path.insert(0, '" + (bazel_bin / "src" / "task" / "export").string() + "')\n";
        ps += "sys.path.insert(0, '" + (bazel_bin / "src" / "test" / "export").string() + "')\n";
        ps += "sys.path.insert(0, '" + (bazel_bin / "src" / "solver" / "export").string() + "')\n";
        ps += "sys.path.insert(0, '" + cwd.string() + "/src')\n";
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
        } else if (arg == "-i") {
            interactive = true;
        } else if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        } else if (!arg.empty() && arg[0] != '-') {
            script_path = arg;
        }
    }

    auto& cfg = Config::instance();
    auto& proc = ProcessInfo::instance();

    proc.set_worker_mode(worker_mode);
    proc.set_worker_id(worker_id);
    proc.set_master_host(master_host);
    proc.set_master_port(master_port);
    proc.set_cli_master_port(master_port);
    cfg.set_str("log_dir", log_dir);
    proc.set_interactive(interactive);
    proc.set_script_path(script_path);
    proc.set_worker_attributes(worker_attributes);

    if (!host_override.empty()) {
        proc.set_hostname(host_override);
    }

    if (!worker_mode) {
        log_dir = fly::Logger::resolve_log_dir(log_dir);
        cfg.set_str("log_dir", log_dir);
    }
    fly::Logger::init(log_dir, worker_id);

    wchar_t** wargv = new wchar_t*[argc];
    for (int i = 0; i < argc; ++i) {
        wargv[i] = Py_DecodeLocale(argv[i], nullptr);
    }

    Py_Initialize();
    PySys_SetArgv(argc, wargv);
    setup_sys_path();

    int rc = PyRun_SimpleString(
        "import sys\n"
        "from fly.main import run\n"
        "sys.exit(run())\n"
    );

    fly::Logger::shutdown();

    Py_Finalize();

    for (int i = 0; i < argc; ++i) {
        PyMem_RawFree(wargv[i]);
    }
    delete[] wargv;

    return rc != 0 ? 1 : 0;
}
