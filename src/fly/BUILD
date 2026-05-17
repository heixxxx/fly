package(default_visibility = ["//visibility:public"])

py_library(
    name = "fly_py",
    srcs = [
        "__init__.py",
        "agent.py",
        "config.py",
        "database.py",
        "executor.py",
        "log_setup.py",
        "main.py",
        "runtime.py",
        "task.py",
    ],
    deps = [
        "//src/storage/export:_fly_storage.so",
        "//src/agent/export:_fly_agent.so",
        "//src/core/export:_fly_core.so",
        "//src/log/export:_fly_log.so",
    ],
)
