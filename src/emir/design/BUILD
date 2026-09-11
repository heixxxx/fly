# emir design db 的 Python 包入口（R1 批次补接线：emir/__init__ 引用
# emir.design，design 包须进 emir_pkg 的 runfiles 链——同 lib_pkg/
# project_pkg 先例）。

package(default_visibility = ["//visibility:public"])

load("@rules_python//python:defs.bzl", "py_library")

py_library(
    name = "design_pkg",
    srcs = ["__init__.py"],
    deps = ["//src/emir/design/py:fly_emir_design_py"],
)
