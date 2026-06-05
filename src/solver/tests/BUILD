package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_test")

cc_test(
    name = "solver_test",
    srcs = ["solver_test.cpp"],
    copts = ["-std=c++20"],
    deps = [
        "@googletest//:gtest_main",
        "//src/solver/cpp:fly_solver",
    ],
)
