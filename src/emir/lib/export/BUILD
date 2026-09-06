# emir lib 库 db 的 Python 绑定（_fly_emir_lib.so）
# 直接静态内嵌 fly_emir_lib（含 liberty 预编译库），自包含、无运行时查找。

package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_binary")

cc_binary(
    name = "_fly_emir_lib.so",
    srcs = ["lib_export.cpp"],
    deps = [
        "@nanobind//:nanobind_src",
        "//src/export/cpp:fly_export_macros",
        "//src/emir/lib/cpp:fly_emir_lib",
    ],
    copts = ["-std=c++20"],
    linkshared = True,
)
