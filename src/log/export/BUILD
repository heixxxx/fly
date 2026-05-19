package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_binary")

cc_binary(
    name = "_fly_log.so",
    srcs = ["log_export.cpp"],
    deps = [
        "@nanobind//:nanobind_src",
        "//src/export/cpp:fly_export_macros",
        "//src/log/cpp:fly_log",
    ],
    copts = ["-std=c++20"],
    linkopts = ["-lpthread"],
    linkshared = True,
    dynamic_deps = [
        "//src/log/cpp:fly_log_so",
    ],
)