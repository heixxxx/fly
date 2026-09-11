# src/geometry/tests —— geometry_types + transform 单测

package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_test")

cc_test(
    name = "geometry_types_test",
    srcs = ["geometry_types_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/geometry:fly_geometry",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "transform_test",
    srcs = ["transform_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/geometry:fly_geometry",
    ],
    copts = ["-std=c++20"],
)
