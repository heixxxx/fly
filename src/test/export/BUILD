package(default_visibility = ["//visibility:public"])

cc_binary(
    name = "_fly_test.so",
    srcs = ["test_export.cpp"],
    deps = [
        "@nanobind//:nanobind_src",
        "//src/export/cpp:fly_export_macros",
        "//src/serialization/cpp:fly_serialization",
        "//src/storage/cpp:fly_storage",
        "//src/test/cpp:fly_test_object",
    ],
    copts = ["-std=c++20"],
    linkshared = True,
)
