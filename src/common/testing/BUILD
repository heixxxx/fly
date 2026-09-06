# common/testing —— 跨模块测试设施（wait_for 等通用测试工具）

package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "fly_common_testing",
    hdrs = glob(["cpp/*.h"]),
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
    deps = [
        "//src/container/cpp:fly_container_aliases",
    ],
    visibility = ["//visibility:public"],
)
