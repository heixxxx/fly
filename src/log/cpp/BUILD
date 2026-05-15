package(default_visibility = ["//visibility:public"])

cc_library(
    name = "fly_log",
    hdrs = ["logger.h"],
    srcs = ["logger.cpp"],
    deps = [
        "//src/common/cpp:fly_common_types",
    ],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
)