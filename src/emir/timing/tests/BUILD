package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_test")

cc_test(
    name = "tm_parser_test",
    srcs = ["tm_parser_test.cpp"],
    data = [
        "data/bad_syntax.twf",
        "data/pg_grid.twf",
        "data/sample.twf",
        "data/tm_design.twf",
        "data/tm_design_mixed.twf",
        "data/tm_design_pins.twf",
    ],
    deps = [
        "@googletest//:gtest_main",
        "//src/emir/timing/cpp:fly_emir_timing",
    ],
    linkstatic = True,
    copts = ["-std=c++20"],
)

# 落库形态 + 名字换算 + 分区合并单测（合成 design db 快照，无外部数据）
cc_test(
    name = "tm_partition_test",
    srcs = ["tm_partition_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/common/testing:fly_common_testing",
        "//src/emir/design/cpp:fly_emir_design_ds_types",
        "//src/emir/timing/cpp:fly_emir_timing",
    ],
    linkstatic = True,
    copts = ["-std=c++20"],
)
