# Fly common type definitions

package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "fly_common_types",
    hdrs = ["common_types.h", "error_types.h", "worker_context.h", "writer_id.h"],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
)