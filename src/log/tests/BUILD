package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_test")

cc_test(
    name = "logger_test",
    srcs = ["logger_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/log/cpp:fly_log",
    ],
    copts = ["-std=c++20"],
)
