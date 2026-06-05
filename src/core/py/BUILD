# Fly core Python package

package(default_visibility = ["//visibility:public"])

load("@rules_python//python:defs.bzl", "py_library")

py_library(
    name = "fly_core_py",
    srcs = glob(["*.py"]),
    deps = ["//src/core/export:_fly_core.so"],
)