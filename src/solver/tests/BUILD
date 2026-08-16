package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_test")
load("@rules_python//python:defs.bzl", "py_test")

cc_test(
    name = "solver_test",
    srcs = ["solver_test.cpp"],
    copts = ["-std=c++20"],
    deps = [
        "@googletest//:gtest_main",
        "//src/solver/cpp:fly_solver",
    ],
)

# ras_graph 矩阵文件 IO 原子性（P3-24 修复回归防护）
py_test(
    name = "ras_graph_io_test",
    srcs = ["test_ras_graph_io.py"],
    main = "test_ras_graph_io.py",
    deps = ["//src/solver/py:fly_solver_py"],
)
