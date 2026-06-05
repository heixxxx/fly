package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_library")
load("@rules_cc//cc:cc_shared_library.bzl", "cc_shared_library")

cc_library(
    name = "fly_solver",
    srcs = ["solver.cpp"],
    hdrs = ["solver.h"],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
    linkstatic = False,
    deps = [
        "@eigen//:eigen",
        "//src/common/cpp:fly_common_types",
    ],
)

cc_shared_library(
    name = "fly_solver_so",
    deps = [":fly_solver"],
    visibility = ["//visibility:public"],
)
