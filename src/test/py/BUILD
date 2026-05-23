package(default_visibility = ["//visibility:public"])

load("@rules_python//python:defs.bzl", "py_library")

py_library(
    name = "fly_test_py",
    srcs = ["__init__.py", "e2e_tasks.py", "test_tasks.py"],
    deps = [
        "//src/test/export:_fly_test.so",
        "//src/fly:fly_py",
    ],
    imports = [
        "../export",
        "../../fly",
    ],
)
