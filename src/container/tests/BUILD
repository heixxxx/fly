package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_test")

cc_test(
    name = "lookup_table_test",
    srcs = ["lookup_table_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/container/cpp:fly_container",
    ],
    copts = ["-std=c++20"],
)

# 原 common_types_test：CM* 别名（现 container_aliases.h）的行为测试
cc_test(
    name = "container_aliases_test",
    srcs = ["container_aliases_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/container/cpp:fly_container_aliases",
    ],
    copts = ["-std=c++20"],
)
