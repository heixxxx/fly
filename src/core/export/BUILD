package(default_visibility = ["//visibility:public"])

cc_binary(
    name = "_fly_core.so",
    srcs = ["core_export.cpp"],
    deps = [
        "@nanobind//:nanobind_src",
        "//src/export/cpp:fly_export_macros",
        "//src/core/cpp:fly_core",
        "//src/serialization/cpp:fly_serialization",
    ],
    copts = ["-std=c++20"],
    linkshared = True,
)
