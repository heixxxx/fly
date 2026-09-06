# common/buffer —— FlyBuffer 字节缓冲 + data_checksum 校验稳定包装层
# （data_checksum_so：多 so 共享的单一定义点，libisal 依赖收敛于此）

package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_shared_library", "cc_test")

cc_library(
    name = "fly_common_buffer",
    hdrs = ["cpp/fly_buffer.h"],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
    deps = [
        "//src/common/types:fly_common_types",
        "//src/container/cpp:fly_container_aliases",
    ],
    visibility = ["//visibility:public"],
)

# 数据校验稳定包装层（契约见 data_checksum.h 头注释）。
# 实现细节（ISA-L）封闭在本 target：换校验方案只改 data_checksum.cpp。
# linkopts 直接声明（同 fly_storage 的 lz4 模式）：cc_shared_library 只透传
# 依赖 cc_library 自身的 linkopts，不透传其 deps 的 third_party linkopts。
cc_library(
    name = "data_checksum",
    srcs = ["cpp/data_checksum.cpp"],
    hdrs = ["cpp/data_checksum.h"],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
    linkopts = ["-lisal"],
    deps = ["//third_party:isal"],
    visibility = ["//visibility:public"],
)

cc_shared_library(
    name = "data_checksum_so",
    deps = [":data_checksum"],
    visibility = ["//visibility:public"],
)

cc_test(
    name = "data_checksum_test",
    srcs = ["tests/data_checksum_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/common/buffer:data_checksum",
    ],
    copts = ["-std=c++20"],
)
