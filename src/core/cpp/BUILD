# Fly core C++ library

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "fly_core_cpp",
    srcs = ["config.cpp"],
    hdrs = ["config.h"],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
    deps = ["//src/common/cpp:fly_common_types"],
)