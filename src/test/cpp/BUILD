package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "fly_test_object",
    srcs = [],
    hdrs = ["test_object.h"],
    strip_include_prefix = "/src",
    deps = [
        "//src/container/cpp:fly_container_aliases",
        "//src/common/serialization:fly_serialization",
    ],
    copts = ["-std=c++20"],
)
