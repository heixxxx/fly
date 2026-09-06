package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_test")

cc_test(
    name = "lib_parser_test",
    srcs = ["lib_parser_test.cpp"],
    data = ["data/minitest.lib"],
    deps = [
        "@googletest//:gtest_main",
        "//src/emir/lib/cpp:fly_emir_lib",
    ],
    # 静态链 fly_emir_lib（含预编译 liberty so 的链接输入）——
    # 解析逻辑验证不经过 cc_shared_library 绑定层
    linkstatic = True,
    copts = ["-std=c++20"],
)
