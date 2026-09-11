# src/lefdef/tests —— 解析基线单测（演进三步走第 2 步）。
# LEF/DEF 两个 driver 独立翻译单元（各自 include 上游头，隔离 lex 层
# 实体）；金标数据经 data 引用上游 TEST 目录，测试内 TEST_SRCDIR 定位。

package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_test")

cc_test(
    name = "baseline_parse_test",
    srcs = [
        "baseline_drivers.hpp",
        "baseline_parse_test.cpp",
        "def_baseline.cpp",
        "lef_baseline.cpp",
    ],
    data = [
        "//src/lefdef:def/TEST/complete.5.8.def",
        "//src/lefdef:lef/TEST/complete.5.8.lef",
    ],
    deps = [
        "@googletest//:gtest_main",
        "//src/lefdef:fly_lefdef_def",
        "//src/lefdef:fly_lefdef_lef",
    ],
    copts = ["-std=c++20"],
)
