package(default_visibility = ["//visibility:public"])

load("@rules_python//python:defs.bzl", "py_library")

py_library(
    name = "fly_agent_py",
    srcs = [
        "__init__.py",
        "agent.py",
        "executor.py",
    ],
    deps = [
        "//src/agent/export:_fly_agent.so",
        "//src/storage/export:_fly_storage.so",
        "//src/log/export:_fly_log.so",
        "//src/core/export:_fly_core.so",
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
