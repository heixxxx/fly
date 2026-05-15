package(default_visibility = ["//visibility:public"])

cc_library(
    name = "gtest",
    srcs = glob([
        "googletest/src/*.cc",
        "googletest/src/*.h",
    ]),
    hdrs = glob(["googletest/include/gtest/**/*.h"]),
    includes = [
        "googletest",
        "googletest/include",
        "googletest/src",
    ],
    copts = ["-std=c++20"],
)

cc_library(
    name = "gtest_main",
    srcs = ["googletest/src/gtest_main.cc"],
    deps = [":gtest"],
    copts = ["-std=c++20"],
)