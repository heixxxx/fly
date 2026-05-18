#include <Python.h>
#include <cstdio>
#include <string>
#include <filesystem>

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

int main(int argc, char* argv[]) {
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
        "sys.exit(run(sys.argv))\n"
    );

    Py_Finalize();

    for (int i = 0; i < argc; ++i) {
        PyMem_RawFree(wargv[i]);
    }
    delete[] wargv;

    return rc != 0 ? 1 : 0;
}
