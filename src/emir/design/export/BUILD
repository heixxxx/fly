# emir design 模块的 Python 绑定（_fly_emir_design.so）
# 直接静态内嵌 fly_emir_design_ds_types，自包含、无运行时查找。

package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_binary")

cc_binary(
    name = "_fly_emir_design.so",
    srcs = ["design_export.cpp"],
    deps = [
        "@nanobind//:nanobind_src",
        "//src/export/cpp:fly_export_macros",
        "//src/emir/design/cpp:fly_emir_design_ds_types",
    ],
    copts = ["-std=c++20"],
    linkshared = True,
)
