package(default_visibility = ["//visibility:public"])

cc_test(
    name = "logger_test",
    srcs = ["logger_test.cpp"],
    deps = [
        "@com_google_googletest//:gtest_main",
        "//src/log/cpp:fly_log",
    ],
    copts = ["-std=c++20"],
)
