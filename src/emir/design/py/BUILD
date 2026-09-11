# Fly emir design Python package

package(default_visibility = ["//visibility:public"])

load("@rules_python//python:defs.bzl", "py_library")

py_library(
    name = "fly_emir_design_py",
    srcs = glob(["*.py"]),
    deps = [
        "//src/emir/design/export:_fly_emir_design.so",
        "//src/emir/lib/py:fly_emir_lib_py",  # load 顺序：lib 先于 design
        "//src/emir/project:project_pkg",
        "//src/fly:fly_py",
        "//src/log:log_pkg",  # ds_db.py: from log import INFO
        "//src/storage:storage_pkg",
    ],
)
