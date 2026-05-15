package(default_visibility = ["//visibility:public"])

cc_test(
    name = "logger_test",
    srcs = ["logger_test.cpp"],
    deps = [
        "//src/log/cpp:fly_log",
        "@com_google_googletest//:gtest_main",
    ],
    copts = ["-std=c++20"],
)