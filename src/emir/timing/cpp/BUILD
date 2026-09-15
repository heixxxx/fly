# emir timing db 的 C++ 核心：TWF（时序窗口文件）解析器 + 解析边界
# 领域结构。当前增量 = 解析器立项（2026-09-15）：py/export 与 db flow
# 随建库流程立项补全（dev-rules 三段式结构标准不变）。

package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "fly_emir_timing",
    srcs = [
        "tm_parser.cpp",
        "tm_types.cpp",
    ],
    hdrs = [
        "tm_parser.h",
        "tm_types.h",
    ],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
    deps = [
        "//src/container/cpp:fly_container_aliases",
        "//src/common/serialization:fly_serialization",
    ],
    visibility = ["//visibility:public"],
)
