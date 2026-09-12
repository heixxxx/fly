package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_test")
load("@rules_python//python:defs.bzl", "py_test")

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

# lib flow 错误处理兜底单测（流程错误处理范式 2026-09-13，dev-rules §7.2）
py_test(
    name = "lib_flow_error_test",
    srcs = ["test_lib_flow_error.py"],
    main = "test_lib_flow_error.py",
    imports = ["../../..", "../export", "../../design/export"],
    deps = [
        "//src/emir:emir_pkg",
        "//src/emir/lib/export:_fly_emir_lib.so",
        "//src/fly:fly_py",
        "//src/log/export:_fly_log.so",
    ],
)
