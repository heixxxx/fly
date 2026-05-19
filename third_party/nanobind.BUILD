load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "nanobind",
    hdrs = glob([
        "include/nanobind/**/*.h",
    ]),
    includes = ["include"],
    copts = ["-std=c++20"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "nanobind_src",
    srcs = [
        "src/nb_combined.cpp",
    ],
    hdrs = glob([
        "include/nanobind/**/*.h",
        "src/*.h",
        "src/*.cpp",
    ]),
    includes = ["include", "src"],
    deps = ["@robin_map//:robin_map"],
    copts = [
        "-std=c++20",
        "-DNB_COMPACT_ASSERTIONS",
    ],
    linkstatic = True,
    alwayslink = True,
    visibility = ["//visibility:public"],
)
