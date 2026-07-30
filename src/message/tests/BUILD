package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_test")

cc_test(
    name = "message_registry_test",
    srcs = ["message_registry_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/message/cpp:fly_message",
    ],
    copts = ["-std=c++20"],
)
