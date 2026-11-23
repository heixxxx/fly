# Fly emir waveform Python package

package(default_visibility = ["//visibility:public"])

load("@rules_python//python:defs.bzl", "py_library")

py_library(
    name = "fly_emir_waveform_py",
    srcs = glob(["*.py"]),
    deps = [
        "//src/emir/common:fly_emir_common_py",  # alpha_settings.py 基座
        "//src/emir/design/py:fly_emir_design_py",  # 快照消费：DesignDb 常量/mapper
        "//src/emir/project:project_pkg",
        "//src/emir/waveform/export:_fly_emir_waveform.so",
        "//src/fly:fly_py",
        "//src/log:log_pkg",  # wf_flow/wf_db: from log import INFO
        "//src/storage:storage_pkg",
        "//src/task:task_pkg",  # wait_obj（快照组装阻塞等待）
    ],
)
