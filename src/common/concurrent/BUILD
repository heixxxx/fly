# common/concurrent —— 线程安全容器封装与写者优先读写锁

package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "fly_common_concurrent",
    hdrs = glob(["cpp/*.h"]),
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
    deps = [
        "//src/container/cpp:fly_container_aliases",
    ],
    visibility = ["//visibility:public"],
)
