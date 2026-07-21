package(default_visibility = ["//visibility:public"])

load("@rules_python//python:defs.bzl", "py_library")
load("@pip//:requirements.bzl", "requirement")

py_library(
    name = "fly_py",
    srcs = glob(["*.py"]),
    deps = [
        "//src/agent/py:fly_agent_py",
        "//src/core/py:fly_core_py",
        "//src/log/export:_fly_log.so",
        "//src/storage/py:fly_storage_py",
        "//src/task:task_pkg",
        requirement("cloudpickle"),  # mapreduce.py: fn serialization
    ],
    imports = [
        "..",
        "../storage/py",
        "../core/py",
        "../task/py",
        "../agent/py",
        "../log/export",
        "../storage/export",
        "../core/export",
        "../agent/export",
        "../task/export",
        "../network/export",
    ],
)
