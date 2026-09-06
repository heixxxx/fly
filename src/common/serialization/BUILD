# common/serialization —— 序列化机制（原顶层 serialization 模块迁入）：
# FLY_SERIALIZE 宏（类型能力声明）+ FLY_ENCODE/FLY_DECODE（使用点操作）+
# 对象头（FLY_OBJECT_MAGIC）+ bitsery 版本化扩展

package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_shared_library")

cc_library(
    name = "fly_serialization",
    srcs = glob(["cpp/*.cpp"]),
    hdrs = glob(["cpp/*.h", "cpp/**/*.h"]),
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
    linkstatic = False,
    deps = [
        "@bitsery//:bitsery",
        "//src/common/buffer:data_checksum",
        "//src/common/buffer:fly_common_buffer",
        "//src/common/types:fly_common_types",
        "//src/container/cpp:fly_container_aliases",
        "//third_party:boost_preprocessor",
    ],
    visibility = ["//visibility:public"],
)

cc_shared_library(
    name = "fly_serialization_so",
    deps = [":fly_serialization"],
    dynamic_deps = ["//src/common/buffer:data_checksum_so"],
    visibility = ["//visibility:public"],
)
