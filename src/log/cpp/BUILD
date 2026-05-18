package(default_visibility = ["//visibility:public"])

cc_library(
    name = "fly_log",
    srcs = ["logger.cpp"],
    hdrs = ["logger.h"],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
    linkstatic = False,
    deps = [
        "//src/common/cpp:fly_common_types",
    ],
)
