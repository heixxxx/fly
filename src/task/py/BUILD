package(default_visibility = ["//visibility:public"])

load("@rules_python//python:defs.bzl", "py_library")

py_library(
    name = "fly_task_py",
    srcs = [
        "__init__.py",
        "task.py",
    ],
    deps = [
        "//src/agent/export:_fly_agent.so",
    ],
    imports = [
        "../agent/export",
        "../storage/export",
        "../log/export",
        "../core/export",
        "..",
    ],
)
