# Fly common types tests

package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_test")

cc_test(
    name = "common_types_test",
    srcs = ["common_types_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/common/cpp:fly_common_types",
    ],
    copts = ["-std=c++20"],
)
