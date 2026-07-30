package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_binary")

cc_binary(
    name = "_fly_message.so",
    srcs = ["message_export.cpp"],
    deps = [
        "@nanobind//:nanobind_src",
        "//src/export/cpp:fly_export_macros",
        "//src/message/cpp:fly_message",
    ],
    copts = ["-std=c++20"],
    linkshared = True,
    dynamic_deps = [
        "//src/message/cpp:fly_message_so",
    ],
)
