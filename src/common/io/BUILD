# common/io —— FdHandle 所有权原语 + 拉取式流式输入源

package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "fly_common_io",
    hdrs = glob(["cpp/*.h"]),
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
    deps = [
        "//src/container/cpp:fly_container_aliases",
    ],
    visibility = ["//visibility:public"],
)
