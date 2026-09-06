# Fly emir lib Python package

package(default_visibility = ["//visibility:public"])

load("@rules_python//python:defs.bzl", "py_library")

py_library(
    name = "fly_emir_lib_py",
    srcs = glob(["*.py"]),
    deps = [
        "//src/emir/lib/export:_fly_emir_lib.so",
        "//src/emir/project:project_pkg",
        "//src/fly:fly_py",
        "//src/storage:storage_pkg",
    ],
)
