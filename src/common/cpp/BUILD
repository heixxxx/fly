# Fly common type definitions

package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_library")
load("@rules_cc//cc:cc_shared_library.bzl", "cc_shared_library")

cc_library(
    name = "fly_common_types",
    hdrs = glob(["*.h"]),
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
)

# 数据校验稳定包装层（契约见 data_checksum.h 头注释）。
# 实现细节（ISA-L）封闭在本 target：换校验方案只改 data_checksum.cpp。
# linkopts 直接声明（同 fly_storage 的 lz4 模式）：cc_shared_library 只透传
# 依赖 cc_library 自身的 linkopts，不透传其 deps 的 third_party linkopts。
cc_library(
    name = "data_checksum",
    srcs = ["data_checksum.cpp"],
    hdrs = ["data_checksum.h"],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
    linkopts = ["-lisal"],
    deps = ["//third_party:isal"],
    visibility = ["//visibility:public"],
)

# 独立 .so：serialization/storage 等多个 so 共享同一份实现（避免各自静态
# 链入造成符号重复定义），也把 libisal2 的动态链接依赖收敛到单一定义点。
cc_shared_library(
    name = "data_checksum_so",
    deps = [":data_checksum"],
    visibility = ["//visibility:public"],
)