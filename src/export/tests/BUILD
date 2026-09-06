package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_test")

cc_test(
    name = "export_test",
    srcs = ["export_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/export/cpp:fly_export_macros",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "export_functional_test",
    srcs = ["export_functional_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/export/cpp:fly_export_macros",
        "//src/common/serialization:fly_serialization",
    ],
    copts = ["-std=c++20"],
)
