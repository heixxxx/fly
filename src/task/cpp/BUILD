package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_library")
load("@rules_cc//cc:cc_shared_library.bzl", "cc_shared_library")

cc_library(
    name = "fly_task",
    srcs = [
        "dependency_graph.cpp",
        "worker_manager.cpp",
        "task_scheduler.cpp",
        "task_manager.cpp",
        "heartbeat_monitor.cpp",
    ],
    hdrs = [
        "dependency_graph.h",
        "heartbeat_monitor.h",
        "task_manager.h",
        "task_scheduler.h",
        "worker_manager.h",
    ],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
    linkstatic = False,
    deps = [
        "//src/common/cpp:fly_common_types",
    ],
)


cc_shared_library(
    name = "fly_task_so",
    deps = [":fly_task"],
    visibility = ["//visibility:public"],
)