# Fly storage Python export bindings
# Built as cc_binary with linkshared for proper Python extension module (.so)

package(default_visibility = ["//visibility:public"])

cc_binary(
    name = "_fly_storage.so",
    srcs = ["storage_export.cpp"],
    deps = [
        "@nanobind//:nanobind_src",
        "//src/storage/cpp:fly_storage_database",
        "//src/storage/cpp:fly_storage_data_service",
        "//src/storage/cpp:fly_storage_storage_manager",
        "//src/storage/cpp:fly_storage_db_meta",
        "//src/storage/cpp:fly_storage_object",
        "//src/storage/cpp:fly_storage_index_entry",
        "//src/storage/cpp:fly_storage_compression_impl",
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