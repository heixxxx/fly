# Fly emir waveform db 子模块（⑧ waveform db）：cpp 核心 + export 绑定 +
# py 流程编排（嵌套包结构，同 design/lib/timing 先例）

package(default_visibility = ["//visibility:public"])

load("@rules_python//python:defs.bzl", "py_library")

py_library(
    name = "waveform_pkg",
    srcs = ["__init__.py"],
    deps = [
        "//src/emir/common:fly_emir_common_py",
        "//src/emir/project:project_pkg",
        "//src/emir/waveform/py:fly_emir_waveform_py",
    ],
)
