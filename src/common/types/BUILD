# common/types —— 基础别名（指针族）：永久稳定层，零依赖

package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "fly_common_types",
    hdrs = glob(["cpp/*.h"]),
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
    visibility = ["//visibility:public"],
)
