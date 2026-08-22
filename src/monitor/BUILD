package(default_visibility = ["//visibility:public"])

load("@rules_python//python:defs.bzl", "py_library")

# monitor 模块 Python 包根（task_io 归属组件 + serve Web GUI）。
py_library(
    name = "monitor_pkg",
    srcs = ["__init__.py"],
    deps = ["//src/monitor/py:fly_monitor_py"],
)
