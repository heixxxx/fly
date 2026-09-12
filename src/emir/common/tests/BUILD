package(default_visibility = ["//visibility:public"])

load("@rules_python//python:defs.bzl", "py_test")

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
