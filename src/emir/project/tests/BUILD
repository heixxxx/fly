package(default_visibility = ["//visibility:public"])

load("@rules_python//python:defs.bzl", "py_test")

py_test(
    name = "emir_project_test",
    srcs = ["test_emir_project.py"],
    main = "test_emir_project.py",
    imports = ["../../lib/export", "../../design/export",
               "../../timing/export",
           ],
    deps = [
        "//src/emir:emir_pkg",
        "//src/emir/design/export:_fly_emir_design.so",
        "//src/emir/lib/export:_fly_emir_lib.so",
        "//src/emir/timing/export:_fly_emir_timing.so",
        "//src/fly:fly_py",
    ],
)
