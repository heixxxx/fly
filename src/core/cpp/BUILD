package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_library")
load("@rules_cc//cc:cc_shared_library.bzl", "cc_shared_library")

cc_library(
    name = "fly_core",
    srcs = ["config.cpp", "graceful_exit.cpp", "process_info.cpp"],
    hdrs = ["config.h", "graceful_exit.h", "process_info.h"],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
    linkstatic = False,
    deps = [
        "//src/common/cpp:fly_common_types",
    ],
)


cc_shared_library(
    name = "fly_core_so",
    deps = [":fly_core"],
    visibility = ["//visibility:public"],
)