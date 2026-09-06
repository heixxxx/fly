package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_binary")

# 自包含绑定：静态内嵌 fly_container，无运行时 so 查找（同 _fly_emir_lib 模式）。
cc_binary(
    name = "_fly_container.so",
    srcs = ["container_export.cpp"],
    deps = [
        "@nanobind//:nanobind_src",
        "//src/container/cpp:fly_container",
        "//src/export/cpp:fly_export_macros",
    ],
    copts = ["-std=c++20"],
    linkshared = True,
)
