package(default_visibility = ["//visibility:public"])

load("@rules_python//python:defs.bzl", "py_library")

py_library(
    name = "emir_pkg",
    srcs = ["__init__.py"],
    deps = [
        "//src/emir/common:fly_emir_common_py",
        "//src/emir/design:design_pkg",
        "//src/emir/lib:lib_pkg",
        "//src/emir/project:project_pkg",
        "//src/emir/spef:spef_pkg",
        "//src/emir/timing:timing_pkg",
        "//src/emir/waveform:waveform_pkg",
    ],
)
