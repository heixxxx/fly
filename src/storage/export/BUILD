package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_binary")

cc_binary(
    name = "_fly_storage.so",
    srcs = ["storage_export.cpp"],
    deps = [
        "@nanobind//:nanobind_src",
        "//src/export/cpp:fly_export_macros",
        "//src/storage/cpp:fly_storage",
        "//src/serialization/cpp:fly_serialization",
    ],
    copts = ["-std=c++20"],
    linkshared = True,
    dynamic_deps = [
        "//src/storage/cpp:fly_storage_so",
    ],
)
