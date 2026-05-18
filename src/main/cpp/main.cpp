#include <Python.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <filesystem>
#include <cstdlib>

static void setup_sys_path() {
    std::filesystem::path real_exe = std::filesystem::canonical("/proc/self/exe");
    std::filesystem::path bazel_bin = real_exe.parent_path().parent_path().parent_path().parent_path();
    std::filesystem::path cwd = std::filesystem::current_path();

    std::string ps = "import sys, os\n";
    ps += "sys.path.insert(0, '" + (bazel_bin / "src" / "core" / "export").string() + "')\n";
    ps += "sys.path.insert(0, '" + (bazel_bin / "src" / "storage" / "export").string() + "')\n";
    ps += "sys.path.insert(0, '" + (bazel_bin / "src" / "agent" / "export").string() + "')\n";
    ps += "sys.path.insert(0, '" + (bazel_bin / "src" / "log" / "export").string() + "')\n";
    ps += "sys.path.insert(0, '" + (bazel_bin / "src" / "network" / "export").string() + "')\n";
    ps += "sys.path.insert(0, '" + (bazel_bin / "src" / "test" / "export").string() + "')\n";
    ps += "sys.path.insert(0, '" + cwd.string() + "/src')\n";

    PyRun_SimpleString(ps.c_str());
}

static std::string escape_py(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '\'') out += "\\'";
        else if (c == '\\') out += "\\\\";
        else out += c;
    }
    return out;
}

static void fly_cleanup() {
    PyRun_SimpleString(
        "try:\n"
        "    from fly.runtime import get_agent, reset\n"
        "    agent = get_agent()\n"
        "    if agent is not None:\n"
        "        reset()\n"
        "except Exception:\n"
        "    pass\n"
    );
    PyRun_SimpleString(
        "try:\n"
        "    from _fly_storage import ex_stg_get_data_service\n"
        "    ds = ex_stg_get_data_service()\n"
        "    ds.drain_write_back()\n"
        "    ds.stop_write_back()\n"
        "    ds.stop_transfer_server()\n"
        "except Exception:\n"
        "    pass\n"
    );
}

int main(int argc, char* argv[]) {
    bool worker_mode = false;
    int worker_id = 0;
    std::string master_host = "127.0.0.1";
    int master_port = 0;
    std::string log_dir = "fly_log";
    bool interactive = false;
    std::string script_path;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--worker") == 0) {
            worker_mode = true;
        } else if (std::strcmp(argv[i], "--worker-id") == 0 && i + 1 < argc) {
            worker_id = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--master-host") == 0 && i + 1 < argc) {
            master_host = argv[++i];
        } else if (std::strcmp(argv[i], "--master-port") == 0 && i + 1 < argc) {
            master_port = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--log-dir") == 0 && i + 1 < argc) {
            log_dir = argv[++i];
        } else if (std::strcmp(argv[i], "-i") == 0) {
            interactive = true;
        } else if (argv[i][0] != '-') {
            script_path = argv[i];
        }
    }

    Py_Initialize();
    setup_sys_path();

    int exit_code = 0;

    try {
        if (worker_mode) {
            std::string init_cmd =
                "import fly.main\n"
                "fly.main.init(worker_mode=True, worker_id=" + std::to_string(worker_id) +
                ", master_host='" + escape_py(master_host) +
                "', master_port=" + std::to_string(master_port) +
                ", log_dir='" + escape_py(log_dir) + "')\n";
            int rc = PyRun_SimpleString(init_cmd.c_str());
            if (rc != 0) {
                fprintf(stderr, "Worker init failed\n");
                fly_cleanup();
                Py_Finalize();
                return 1;
            }

            std::string wait_cmd =
                "import time\n"
                "from fly.runtime import get_agent\n"
                "_w = get_agent()\n"
                "while _w._agent.is_running():\n"
                "    _w._agent.poll_task()\n"
                "    time.sleep(0.05)\n";
            PyRun_SimpleString(wait_cmd.c_str());
        } else {
            std::string init_cmd =
                "import fly.main\n"
                "fly.main.init(log_dir='" + escape_py(log_dir) + "')\n";
            int rc = PyRun_SimpleString(init_cmd.c_str());
            if (rc != 0) {
                fprintf(stderr, "Master init failed\n");
                fly_cleanup();
                Py_Finalize();
                return 1;
            }

            if (!script_path.empty()) {
                std::string set_argv =
                    "import sys\n"
                    "sys.argv = ['" + escape_py(script_path) + "']\n"
                    "sys._fly_script_path = '" + escape_py(script_path) + "'\n";
                PyRun_SimpleString(set_argv.c_str());

                FILE* fp = std::fopen(script_path.c_str(), "r");
                if (!fp) {
                    fprintf(stderr, "Cannot open script: %s\n", script_path.c_str());
                    fly_cleanup();
                    Py_Finalize();
                    return 1;
                }
                PyRun_SimpleFile(fp, script_path.c_str());
                std::fclose(fp);
            }

            if (interactive || script_path.empty()) {
                PyRun_InteractiveLoop(stdin, "<stdin>");
            }
        }
    } catch (const std::exception& e) {
        fprintf(stderr, "Fatal error: %s\n", e.what());
        exit_code = 1;
    } catch (...) {
        fprintf(stderr, "Fatal error: unknown exception\n");
        exit_code = 1;
    }

    fly_cleanup();
    Py_Finalize();
    return exit_code;
}
