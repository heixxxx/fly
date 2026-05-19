package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "fly_test_object",
    srcs = [],
    hdrs = ["test_object.h"],
    strip_include_prefix = "/src",
    deps = [
        "//src/common/cpp:fly_common_types",
        "//src/serialization/cpp:fly_serialization",
    ],
    copts = ["-std=c++20"],
)
