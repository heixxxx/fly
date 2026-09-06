# Fly export macros

package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "fly_export_macros",
    hdrs = ["export_macros.h"],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
    deps = [
        "//src/container/cpp:fly_container_aliases",
        "@nanobind//:nanobind",
        "//src/common/serialization:fly_serialization",
        "//src/storage/cpp:fly_storage",
    ],
)