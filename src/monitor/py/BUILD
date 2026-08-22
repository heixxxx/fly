package(default_visibility = ["//visibility:public"])

load("@rules_python//python:defs.bzl", "py_library")

py_library(
    name = "fly_monitor_py",
    srcs = glob(["*.py"]),
    data = glob(["static/**"]),
    # monitor 纯 stdlib（task_io 计时归属 + serve Web GUI），无第三方依赖、
    # 无 C++ 绑定；数据库层在 C++（src/monitor/cpp:fly_monitor）。
    # static/（前端工程 + vendor ECharts）随包分发，serve.py 按 __file__ 定位。
)
