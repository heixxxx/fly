package(default_visibility = ["//visibility:public"])

load("@rules_python//python:defs.bzl", "py_library")

py_library(
    name = "fly_monitor_py",
    srcs = glob(["*.py"]),
    # monitor 纯 stdlib（task_io 计时归属 + serve Web GUI），无第三方依赖、
    # 无 C++ 绑定；数据库层在 C++（src/monitor/cpp:fly_monitor）。
)
