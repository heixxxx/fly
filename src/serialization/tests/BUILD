# Fly serialization tests (gtest)

package(default_visibility = ["//visibility:public"])

cc_test(
    name = "serialization_test",
    srcs = ["serialization_test.cpp"],
    deps = [
        "@com_google_googletest//:gtest_main",
        "//src/serialization/cpp:fly_serialization_macros",
    ],
    copts = ["-std=c++20"],
)