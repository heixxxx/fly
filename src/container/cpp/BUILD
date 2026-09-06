# Fly container 模块 —— 容器别名层（可替换）+ 自定义容器实现
# fly_container_aliases：CM* 容器别名（纯头零依赖，聚合 common/types 指针别名）
# fly_container：自定义容器（lookup_table 等，依赖 serialization 声明序列化能力）

package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_shared_library")

cc_library(
    name = "fly_container_aliases",
    hdrs = ["container_aliases.h"],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
    deps = [
        "//src/common/types:fly_common_types",
    ],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "fly_container",
    srcs = ["lookup_table.cpp"],
    hdrs = ["lookup_table.h"],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
    deps = [
        "//src/common/serialization:fly_serialization",
        "//src/container/cpp:fly_container_aliases",
    ],
    visibility = ["//visibility:public"],
)

cc_shared_library(
    name = "fly_container_so",
    deps = [":fly_container"],
    exports_filter = [
        "//src/common/serialization:fly_serialization",
        "//src/common/buffer:data_checksum",
    ],
    visibility = ["//visibility:public"],
)
