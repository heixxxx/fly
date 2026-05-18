package(default_visibility = ["//visibility:public"])

cc_binary(
    name = "_fly_test.so",
    srcs = ["test_export.cpp"],
    deps = [
        "@nanobind//:nanobind_src",
        "//src/test/cpp:fly_test_object",
        "//src/storage/cpp:fly_storage_cpp_headers",
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
)
