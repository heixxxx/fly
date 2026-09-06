# common/io 测试

package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_test")

cc_test(
    name = "fd_handle_test",
    srcs = ["fd_handle_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/common/io:fly_common_io",
    ],
    copts = ["-std=c++20"],
)
