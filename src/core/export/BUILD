# Fly core Python export bindings
# Built as cc_binary with linkshared for proper Python extension module (.so)

package(default_visibility = ["//visibility:public"])

cc_binary(
    name = "_fly_core.so",
    srcs = ["core_export.cpp"],
    deps = [
        "@nanobind//:nanobind_src",
        "//src/core/cpp:fly_core_cpp",
        "//src/export/cpp:fly_export_macros",
        "//src/serialization/cpp:fly_serialization_macros",
    ],
    copts = [
        "-std=c++20",
        "-I/usr/include/python3.10",
    ],
    linkopts = [
        "",
        "-lpython3.10",
    ],
    linkshared = True,
    linkstatic = True,
)