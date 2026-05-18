# Fly serialization tests (gtest)

package(default_visibility = ["//visibility:public"])

cc_test(
    name = "serialization_test",
    srcs = ["serialization_test.cpp"],
    deps = [
        "@com_google_googletest//:gtest_main",
        "//src/serialization/cpp:fly_serialization",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "object_header_test",
    srcs = ["object_header_test.cpp"],
    deps = [
        "@com_google_googletest//:gtest_main",
        "//src/serialization/cpp:fly_serialization",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "compressing_streambuf_test",
    srcs = ["compressing_streambuf_test.cpp"],
    deps = [
        "@com_google_googletest//:gtest_main",
        "//src/serialization/cpp:fly_serialization",
        "//src/storage/cpp:fly_storage",
    ],
    copts = ["-std=c++20"],
)
