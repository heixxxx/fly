package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_library")
load("@rules_cc//cc:cc_shared_library.bzl", "cc_shared_library")

cc_library(
    name = "fly_serialization",
    srcs = [
        "object_header.cpp",
    ],
    hdrs = [
        "object_header.h",
        "serialization_macros.h",
        "bitsery_ext/version.h",
    ],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
    linkstatic = False,
    deps = [
        "@bitsery//:bitsery",
        "//src/common/cpp:fly_common_types",
        "//third_party:boost_preprocessor",
    ],
)


cc_shared_library(
    name = "fly_serialization_so",
    deps = [":fly_serialization"],
    visibility = ["//visibility:public"],
)