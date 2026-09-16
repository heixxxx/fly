# Fly emir timing db 子模块（⑦ 时序数据库）：cpp 核心 + export 绑定 +
# py 流程编排（嵌套包结构，同 design/lib 先例）

package(default_visibility = ["//visibility:public"])

load("@rules_python//python:defs.bzl", "py_library")

py_library(
    name = "timing_pkg",
    srcs = ["__init__.py"],
    deps = [
        "//src/emir/common:fly_emir_common_py",
        "//src/emir/project:project_pkg",
        "//src/emir/timing/py:fly_emir_timing_py",
    ],
)
