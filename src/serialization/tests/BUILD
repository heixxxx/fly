# Fly serialization tests (gtest)

package(default_visibility = ["//visibility:public"])

cc_test(
    name = "serialization_test",
    srcs = ["serialization_test.cpp"],
    deps = [
        "@com_google_googletest//:gtest_main",
        "//src/serialization/cpp:fly_serialization_macros",
        "//src/common/cpp:fly_common_types",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "object_header_test",
    srcs = ["object_header_test.cpp"],
    deps = [
        "@com_google_googletest//:gtest_main",
        "//src/serialization/cpp:fly_object_header",
        "//src/common/cpp:fly_common_types",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "compressing_streambuf_test",
    srcs = ["compressing_streambuf_test.cpp"],
    deps = [
        "@com_google_googletest//:gtest_main",
        "//src/serialization/cpp:fly_compressing_streambuf",
        "//src/storage/cpp:fly_storage_compression_impl",
        "//src/common/cpp:fly_common_types",
    ],
    copts = ["-std=c++20"],
)