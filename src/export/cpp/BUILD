# Fly export macros

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "fly_export_macros",
    hdrs = ["export_macros.h"],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
    deps = [
        "@nanobind//:nanobind",
        "//src/serialization/cpp:fly_serialization",
    ],
)