# Fly emir lib Python package

package(default_visibility = ["//visibility:public"])

load("@rules_python//python:defs.bzl", "py_library")

py_library(
    name = "fly_emir_lib_py",
    srcs = glob(["*.py"]),
    deps = [
        "//src/emir/common:fly_emir_common_py",  # alpha_settings.py 基座
        "//src/emir/lib/export:_fly_emir_lib.so",
        "//src/emir/project:project_pkg",
        "//src/fly:fly_py",
        "//src/log:log_pkg",  # lib_db.py: from log import INFO
        "//src/storage:storage_pkg",
    ],
)
