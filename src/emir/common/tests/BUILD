package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_test")
load("@rules_python//python:defs.bzl", "py_test")

# EMIR 族实体强类型 id 单测（C++ 实例化健全形态；机制证明在
# common/types 的 strong_id_test）
cc_test(
    name = "emir_ids_test",
    srcs = ["emir_ids_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/container/cpp:fly_container_aliases",
        "//src/emir/common/cpp:fly_emir_common_cpp",
    ],
    copts = ["-std=c++20"],
)

# AlphaSettings 声明式基座单测（2026-09-13 裁定；import emir 触发全链
# 聚合——.so export deps 同 emir_project_test 先例）
py_test(
    name = "alpha_settings_test",
    srcs = ["test_alpha_settings.py"],
    main = "test_alpha_settings.py",
    imports = [
        "../../..",
        "../../design/export",
        "../../lib/export",
    ],
    deps = [
        "//src/emir:emir_pkg",
        "//src/emir/design/export:_fly_emir_design.so",
        "//src/emir/lib/export:_fly_emir_lib.so",
        "//src/fly:fly_py",
        "//src/log/export:_fly_log.so",
    ],
)
