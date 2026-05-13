# nanobind BUILD file
# nanobind is a thin bindings library between C++ and Python

load("@com_google_googletest//:gtest.bzl", "gtest_dep")  # not used, but available

cc_library(
    name = "nanobind",
    hdrs = glob([
        "include/nanobind/**/*.h",
        "include/nanobind/**/*.inc",
    ]),
    includes = ["include"],
    copts = ["-std=c++20"],
    visibility = ["//visibility:public"],
)

# nanobind also has src files for the runtime
cc_library(
    name = "nanobind_src",
    srcs = glob([
        "src/**/*.cpp",
        "src/**/*.h",
    ]),
    hdrs = glob([
        "include/nanobind/**/*.h",
        "include/nanobind/**/*.inc",
    ]),
    includes = ["include", "src"],
    copts = ["-std=c++20", "-fno-exceptions"],
    visibility = ["//visibility:public"],
)