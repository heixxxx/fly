package(default_visibility = ["//visibility:public"])

load("@rules_python//python:defs.bzl", "py_test")

py_test(
    name = "emir_project_test",
    srcs = ["test_emir_project.py"],
    main = "test_emir_project.py",
    imports = ["../../lib/export"],
    deps = [
        "//src/emir:emir_pkg",
        "//src/emir/lib/export:_fly_emir_lib.so",
        "//src/fly:fly_py",
    ],
)
