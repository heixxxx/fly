package(default_visibility = ["//visibility:public"])

cc_binary(
    name = "_fly_test.so",
    srcs = ["test_export.cpp"],
    deps = [
        "@nanobind//:nanobind_src",
        "//src/test/cpp:fly_test_object",
        "//src/storage/cpp:fly_storage_database",
        "//src/storage/cpp:fly_storage_data_service",
        "//src/export/cpp:fly_export_macros",
        "//src/serialization/cpp:fly_serialization_macros",
    ],
    copts = [
        "-std=c++20",
        "-I/usr/local/include/python3.12",
    ],
    linkopts = [
        "-L/usr/local/lib",
        "-lpython3.12",
    ],
    linkshared = True,
    linkstatic = True,
)
