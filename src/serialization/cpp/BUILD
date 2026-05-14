# Fly serialization macros

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "fly_serialization_macros",
    hdrs = ["serialization_macros.h"],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
    deps = [
        "@zpp_bits//:zpp_bits",
        "//src/common/cpp:fly_common_types",
    ],
)