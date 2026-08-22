package(default_visibility = ["//visibility:public"])

load("@rules_python//python:defs.bzl", "py_library")
load("@pip//:requirements.bzl", "requirement")

py_library(
    name = "fly_agent_py",
    srcs = glob(["*.py"]),
    deps = [
        "//src/agent/export:_fly_agent.so",
        "//src/storage/export:_fly_storage.so",
        "//src/log/export:_fly_log.so",
        "//src/core/export:_fly_core.so",
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
