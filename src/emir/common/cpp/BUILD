# Fly emir common C++ 基座——EMIR 族实体强类型 id（单一权威定义点）

package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_library")

# 纯头文件库（无 .so、不进 main.cpp/fly.sh 安装循环——类型定义无链接产物）
cc_library(
    name = "fly_emir_common_cpp",
    hdrs = glob(["*.h"]),
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
    visibility = ["//visibility:public"],
    deps = [
        "//src/common/types:fly_common_types",
    ],
)
