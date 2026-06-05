# Fly solver Python package

package(default_visibility = ["//visibility:public"])

load("@rules_python//python:defs.bzl", "py_library")

py_library(
    name = "fly_solver_py",
    srcs = glob(["*.py"]),
    deps = ["//src/solver/export:_fly_solver.so"],
)
