package(default_visibility = ["//visibility:public"])

load("@rules_python//python:defs.bzl", "py_library")
load("@pip//:requirements.bzl", "requirement")

py_library(
    name = "fly_agent_py",
    srcs = glob(["*.py"]),
    deps = [
        "//src/agent/export:_fly_agent.so",
        "//src/storage:storage_pkg",  # agent.py/executor.py: from storage import
        "//src/log:log_pkg",  # agent.py/executor.py: from log import
        "//src/core:core_pkg",  # agent.py: from core import ex_core_get_config
        "//src/monitor:monitor_pkg",  # executor.py: task_io IO 归属
        requirement("cloudpickle"),  # executor.py: user-task deserialization
    ],
    imports = [
        "../agent/export",
        "../storage/export",
        "../storage/py",
        "../log/export",
        "../core/export",
        "../task/export",
        "../task/py",
        "../test/export",
        "../network/export",
        "..",
    ],
)
