package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_binary")

cc_binary(
    name = "_fly_core.so",
    srcs = ["core_export.cpp"],
    deps = [
        "//src/common/runtime:fly_common_runtime",
        "@nanobind//:nanobind_src",
        "//src/export/cpp:fly_export_macros",
        "//src/core/cpp:fly_core",
        "//src/common/serialization:fly_serialization",
    ],
    copts = ["-std=c++20"],
    linkshared = True,
    dynamic_deps = [
        "//src/core/cpp:fly_core_so",
        "//src/common/serialization:fly_serialization_so",
    ],
)
