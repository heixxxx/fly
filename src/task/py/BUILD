package(default_visibility = ["//visibility:public"])

load("@rules_python//python:defs.bzl", "py_library")
load("@pip//:requirements.bzl", "requirement")

py_library(
    name = "fly_task_py",
    srcs = glob(["*.py"]),
    deps = [
        "//src/log:log_pkg",  # task.py: from log import DBG
        "//src/storage:storage_pkg",  # task.py 函数内: from storage import
        requirement("cloudpickle"),  # task.py: as_task user-script serialization
    ],
    imports = [
        "..",
    ],
)
