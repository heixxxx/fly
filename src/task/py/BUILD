package(default_visibility = ["//visibility:public"])

load("@rules_python//python:defs.bzl", "py_library")
load("@pip//:requirements.bzl", "requirement")

py_library(
    name = "fly_task_py",
    srcs = glob(["*.py"]),
    deps = [
        "//src/agent/export:_fly_agent.so",
        requirement("cloudpickle"),  # task.py: as_task user-script serialization
    ],
    imports = [
        "../agent/export",
        "../storage/export",
        "../log/export",
        "../core/export",
        "..",
    ],
)
