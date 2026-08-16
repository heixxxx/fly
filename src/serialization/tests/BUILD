# Fly serialization tests (gtest)

package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_test")

cc_test(
    name = "serialization_test",
    srcs = ["serialization_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/serialization/cpp:fly_serialization",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "object_header_test",
    srcs = ["object_header_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/serialization/cpp:fly_serialization",
    ],
    copts = ["-std=c++20"],
)
