# Fly core C++ tests (gtest)

package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_test")

cc_test(
    name = "config_test",
    srcs = ["config_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/core/cpp:fly_core",
    ],
    copts = ["-std=c++20"],
)
