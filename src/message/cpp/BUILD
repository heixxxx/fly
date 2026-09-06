package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_library")
load("@rules_cc//cc:cc_shared_library.bzl", "cc_shared_library")

cc_library(
    name = "fly_message",
    srcs = glob(["*.cpp"]),
    hdrs = glob(["*.h"]),
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
    linkstatic = False,
    deps = [
        "//src/container/cpp:fly_container_aliases",
        "//src/log/cpp:fly_log",
    ],
)

# fly_message_so: message 作为独立 shared library。
# fmt/fly_log 的符号由 fly_log_so 动态提供（dynamic_deps），避免 fmt 被重复
# 静态链接又未导出（参照 fly_agent_so 的 dynamic_deps 模式）。
cc_shared_library(
    name = "fly_message_so",
    deps = [":fly_message"],
    dynamic_deps = [
        "//src/log/cpp:fly_log_so",
    ],
    visibility = ["//visibility:public"],
)
