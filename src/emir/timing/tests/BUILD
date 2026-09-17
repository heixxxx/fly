package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_test")
load("@rules_python//python:defs.bzl", "py_test")

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

# timing flow 错误处理单测（同 test_ds_flow_error.py 先例）
py_test(
    name = "test_tm_flow_error",
    srcs = ["test_tm_flow_error.py"],
    main = "test_tm_flow_error.py",
    imports = ["../../../..", "../export", "../../design/export",
               "../../lib/export"],
    deps = [
        "//src/emir:emir_pkg",
        "//src/emir/design/export:_fly_emir_design.so",
        "//src/emir/lib/export:_fly_emir_lib.so",
        "//src/emir/timing/export:_fly_emir_timing.so",
        "//src/fly:fly_py",
        "//src/log/export:_fly_log.so",
    ],
)

# build_timing_db 入口参数校验负例单测（2026-09-17 裁定：header Schema
# 直接校验 raise + 白名单严格模式）
py_test(
    name = "tm_db_validation_test",
    srcs = ["test_tm_db_validation.py"],
    main = "test_tm_db_validation.py",
    imports = ["../../..", "../export", "../../design/export",
               "../../lib/export",
           ],
    deps = [
        "//src/emir:emir_pkg",
        "//src/emir/design/export:_fly_emir_design.so",
        "//src/emir/lib/export:_fly_emir_lib.so",
        "//src/emir/timing/export:_fly_emir_timing.so",
        "//src/fly:fly_py",
        "//src/log/export:_fly_log.so",
    ],
)
