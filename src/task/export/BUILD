package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_binary")

cc_binary(
    name = "_fly_task.so",
    srcs = ["task_export.cpp"],
    deps = [
        "@nanobind//:nanobind_src",
        "//src/export/cpp:fly_export_macros",
        "//src/task/cpp:fly_task",
    ],
    copts = ["-std=c++20"],
    linkopts = ["-lpthread"],
    linkshared = True,
    dynamic_deps = [
        "//src/task/cpp:fly_task_so",
    ],
)