# Fly storage Python package

package(default_visibility = ["//visibility:public"])

load("@rules_python//python:defs.bzl", "py_library")

py_library(
    name = "fly_storage_py",
    srcs = glob(["*.py"]),
    deps = [
        "//src/storage/export:_fly_storage.so",
        "//src/monitor:monitor_pkg",  # database.py: task_io IO 归属计时
    ],
)
