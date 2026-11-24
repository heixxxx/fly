# Fly emir spef Python package

package(default_visibility = ["//visibility:public"])

load("@rules_python//python:defs.bzl", "py_library")

py_library(
    name = "fly_emir_spef_py",
    srcs = glob(["*.py"]),
    deps = [
        "//src/emir/common:fly_emir_common_py",  # alpha_settings.py 基座 + validators
        "//src/emir/design/py:fly_emir_design_py",  # 快照消费：DesignDb 常量/mapper
        "//src/emir/project:project_pkg",
        "//src/emir/spef/export:_fly_emir_spef.so",
        "//src/fly:fly_py",
        "//src/log:log_pkg",  # sp_flow/sp_db: from log import INFO
        "//src/storage:storage_pkg",
        "//src/task:task_pkg",  # wait_obj（快照组装阻塞等待）
    ],
)
