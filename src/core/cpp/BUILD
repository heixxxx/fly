package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_library")
load("@rules_cc//cc:cc_shared_library.bzl", "cc_shared_library")

cc_library(
    name = "fly_core",
    srcs = glob(["*.cpp"]),
    hdrs = glob(["*.h"]),
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
    linkstatic = False,
    deps = [
        "//src/common/runtime:fly_common_runtime",
        "//src/container/cpp:fly_container_aliases",
        "//src/build_info:fly_build_info",
    ],
)


cc_shared_library(
    name = "fly_core_so",
    deps = [":fly_core"],
    visibility = ["//visibility:public"],
)