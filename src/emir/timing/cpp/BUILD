# emir timing db 的 C++ 核心：TWF（时序窗口文件）解析器 + 解析边界
# 领域结构 + 落库形态/名字换算/分区合并（tm_partition，⑦ timing db 建库
# flow 立项；py/export 随 flow 批次补全，dev-rules 三段式结构标准不变）。

package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "fly_emir_timing",
    srcs = [
        "tm_parser.cpp",
        "tm_partition.cpp",
        "tm_types.cpp",
    ],
    hdrs = [
        "tm_parser.h",
        "tm_partition.h",
        "tm_types.h",
    ],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
    deps = [
        "//src/container/cpp:fly_container_aliases",
        "//src/common/serialization:fly_serialization",
        "//src/emir/common/cpp:fly_emir_common_cpp",  # CM 族强类型 id
        "//src/emir/design/cpp:fly_emir_design_ds_types",  # 换算消费 design db 快照
    ],
    visibility = ["//visibility:public"],
)
