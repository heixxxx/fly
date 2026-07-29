# Fly solver Python package

package(default_visibility = ["//visibility:public"])

load("@rules_python//python:defs.bzl", "py_library")
load("@pip//:requirements.bzl", "requirement")

py_library(
    name = "fly_solver_py",
    srcs = glob(["*.py"]),
    deps = [
        "//src/fly:fly_py",  # project.py/flows.py: Project base, register_flow
        "//src/solver/export:_fly_solver.so",
        requirement("numpy"),  # ras_graph.py: sparse matrix assembly
        requirement("scipy"),  # ras_graph.py: sparse.linalg.splu
    ],
)
