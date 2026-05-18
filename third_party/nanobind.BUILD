# nanobind BUILD file (overrides upstream BUILD.bazel)

cc_library(
    name = "nanobind",
    hdrs = glob([
        "include/nanobind/**/*.h",
        "include/nanobind/**/*.inc",
    ]),
    includes = ["include"],
    copts = [
        "-std=c++20",
        "-I/usr/local/include/python3.12",
    ],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "nanobind_src",
    srcs = [
        "src/nb_combined.cpp",
    ],
    hdrs = glob([
        "include/nanobind/**/*.h",
        "include/nanobind/**/*.inc",
        "src/*.h",
        "src/*.cpp",
    ]),
    includes = ["include", "src"],
    deps = ["@robin_map//:robin_map"],
    copts = [
        "-std=c++20",
        "-I/usr/local/include/python3.12",
        "-DNB_COMPACT_ASSERTIONS",
    ],
    linkopts = ["-lpython3.12"],
    linkstatic = True,
    alwayslink = True,
    visibility = ["//visibility:public"],
)