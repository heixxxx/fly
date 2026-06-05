package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_library")
load("@rules_cc//cc:cc_shared_library.bzl", "cc_shared_library")

cc_library(
    name = "fly_log",
    srcs = glob(["*.cpp"]),
    hdrs = glob(["*.h"]),
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
    linkstatic = False,
    deps = [
        "//src/common/cpp:fly_common_types",
        "@fmt//:fmt",
    ],
)


cc_shared_library(
    name = "fly_log_so",
    deps = [":fly_log"],
    visibility = ["//visibility:public"],
)