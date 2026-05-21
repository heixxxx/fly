#include <Python.h>
#include <core/cpp/config.h>
#include <log/cpp/logger.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <filesystem>

static void setup_sys_path() {
    std::filesystem::path cwd = std::filesystem::current_path();
    std::filesystem::path bazel_bin = cwd / "bazel-bin";

    std::string ps = "import sys, os\n";
    ps += "sys.path.insert(0, '" + (bazel_bin / "src" / "core" / "export").string() + "')\n";
    ps += "sys.path.insert(0, '" + (bazel_bin / "src" / "storage" / "export").string() + "')\n";
    ps += "sys.path.insert(0, '" + (bazel_bin / "src" / "agent" / "export").string() + "')\n";
    ps += "sys.path.insert(0, '" + (bazel_bin / "src" / "log" / "export").string() + "')\n";
    ps += "sys.path.insert(0, '" + (bazel_bin / "src" / "network" / "export").string() + "')\n";
    ps += "sys.path.insert(0, '" + (bazel_bin / "src" / "task" / "export").string() + "')\n";
    ps += "sys.path.insert(0, '" + (bazel_bin / "src" / "test" / "export").string() + "')\n";
    ps += "sys.path.insert(0, '" + cwd.string() + "/src')\n";
    ps += "import _fly_core\n";
    ps += "import _fly_log\n";
    ps += "import _fly_storage\n";
    ps += "import _fly_agent\n";
    ps += "import _fly_task\n";
    ps += "import _fly_test\n";

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
    printf("  -i                   Interactive mode\n");
    printf("  script               Python script to execute\n");
}

int main(int argc, char* argv[]) {
    bool worker_mode = false;
    int worker_id = 0;
    std::string master_host = "127.0.0.1";
    int master_port = 0;
    std::string log_dir = "fly_log";
    bool interactive = false;
    std::string script_path;
    std::string worker_attributes;

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
    cfg.set_int("worker_mode", worker_mode ? 1 : 0);
    cfg.set_int("worker_id", worker_id);
    cfg.set_str("master_host", master_host);
    cfg.set_int("master_port", master_port);
    cfg.set_int("cli_master_port", master_port);
    cfg.set_str("log_dir", log_dir);
    cfg.set_int("interactive", interactive ? 1 : 0);
    cfg.set_str("script_path", script_path);
    cfg.set_str("worker_attributes", worker_attributes);

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
