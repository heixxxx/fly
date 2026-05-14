# Fly core C++ tests (gtest)

package(default_visibility = ["//visibility:public"])

cc_test(
    name = "config_test",
    srcs = ["config_test.cpp"],
    deps = [
        "@com_google_googletest//:gtest_main",
        "//src/core/cpp:fly_core_cpp",
    ],
    copts = ["-std=c++20"],
)