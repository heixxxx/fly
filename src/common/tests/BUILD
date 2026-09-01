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

cc_test(
    name = "concurrent_map_test",
    srcs = ["concurrent_map_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/common/cpp:fly_common_types",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "data_checksum_test",
    srcs = ["data_checksum_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/common/cpp:data_checksum",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "concurrent_queue_test",
    srcs = ["concurrent_queue_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/common/cpp:fly_common_types",
    ],
    copts = ["-std=c++20"],
)
