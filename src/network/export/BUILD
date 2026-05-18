package(default_visibility = ["//visibility:public"])

cc_binary(
    name = "_fly_network.so",
    srcs = ["network_export.cpp"],
    deps = [
        "@nanobind//:nanobind_src",
        "//src/network/cpp:fly_network_cpp",
        "//src/export/cpp:fly_export_macros",
        "//src/serialization/cpp:fly_serialization_macros",
    ],
    copts = [
        "-std=c++20",
        "-I/usr/include/python3.12",
    ],
    linkopts = [
        "",
        "-lpython3.12",
        "-lpthread",
    ],
    linkshared = True,
)