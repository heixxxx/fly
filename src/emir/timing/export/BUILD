# emir timing 模块的 Python 绑定（_fly_emir_timing.so）
# 直接静态内嵌 fly_emir_timing（含 design ds_types 依赖），自包含。

package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_binary")

cc_binary(
    name = "_fly_emir_timing.so",
    srcs = ["timing_export.cpp"],
    deps = [
        "@nanobind//:nanobind_src",
        "//src/export/cpp:fly_export_macros",
        "//src/emir/design/cpp:fly_emir_design_ds_types",
        "//src/emir/timing/cpp:fly_emir_timing",
    ],
    copts = ["-std=c++20"],
    linkshared = True,
)
