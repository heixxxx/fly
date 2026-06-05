package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_binary")

cc_binary(
    name = "_fly_solver.so",
    srcs = ["solver_export.cpp"],
    deps = [
        "@nanobind//:nanobind_src",
        "//src/export/cpp:fly_export_macros",
        "//src/solver/cpp:fly_solver",
    ],
    copts = ["-std=c++20"],
    linkshared = True,
    dynamic_deps = [
        "//src/solver/cpp:fly_solver_so",
    ],
)
