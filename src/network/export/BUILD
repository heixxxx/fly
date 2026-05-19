package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_binary")

cc_binary(
    name = "_fly_network.so",
    srcs = ["network_export.cpp"],
    deps = [
        "@nanobind//:nanobind_src",
        "//src/export/cpp:fly_export_macros",
        "//src/network/cpp:fly_network",
    ],
    copts = ["-std=c++20"],
    linkopts = ["-lpthread"],
    linkshared = True,
    dynamic_deps = [
        "//src/network/cpp:fly_network_so",
    ],
)