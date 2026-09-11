# src/geometry —— geometry 独立模块：纯几何通用结构（GEOPoint/GEORect/
# GEOPolygon，GEO 前缀 = 模块标识）+ 放置变换（transform.h：GEOOrientation/
# GEOTransform）。纯头文件库（无 .src 无绑定无注册六步），deps 只引容器
# 别名 + 序列化宏声明。

package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "fly_geometry",
    hdrs = [
        "cpp/geometry_types.h",
        "cpp/transform.h",
    ],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
    deps = [
        "//src/common/serialization:fly_serialization",
        "//src/container/cpp:fly_container_aliases",
    ],
    visibility = ["//visibility:public"],
)
