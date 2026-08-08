package(default_visibility = ["//visibility:public"])

load("@rules_python//python:defs.bzl", "py_library")
load("@pip//:requirements.bzl", "requirement")

py_library(
    name = "fly_py",
    srcs = glob(["*.py"]),
    deps = [
        "//src/agent/py:fly_agent_py",
        "//src/agent:agent_pkg",
        "//src/core/py:fly_core_py",
        "//src/core:core_pkg",
        "//src/log/export:_fly_log.so",
        "//src/log:log_pkg",
        "//src/message/export:_fly_message.so",
        "//src/storage/py:fly_storage_py",
        "//src/storage:storage_pkg",
        "//src/task:task_pkg",
        "//src/network:network_pkg",
        requirement("cloudpickle"),  # mapreduce.py: fn serialization
    ],
    imports = [
        "..",
        "../storage/py",
        "../core/py",
        "../task/py",
        "../agent/py",
        "../log/export",
        "../message/export",
        "../storage/export",
        "../core/export",
        "../agent/export",
        "../task/export",
        "../network/export",
    ],
)
