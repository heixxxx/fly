# common/types/tests —— 公共工具宏单测（property_macro / flags_macro）

package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_test")

cc_test(
    name = "flags_macro_test",
    srcs = ["flags_macro_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/common/types:fly_common_types",
        "//src/container/cpp:fly_container_aliases",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "property_macro_test",
    srcs = ["property_macro_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/common/types:fly_common_types",
        "//src/container/cpp:fly_container_aliases",
    ],
    copts = ["-std=c++20"],
)
