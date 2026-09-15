package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_test")

cc_test(
    name = "tm_parser_test",
    srcs = ["tm_parser_test.cpp"],
    data = [
        "data/bad_syntax.twf",
        "data/sample.twf",
    ],
    deps = [
        "@googletest//:gtest_main",
        "//src/emir/timing/cpp:fly_emir_timing",
    ],
    linkstatic = True,
    copts = ["-std=c++20"],
)
