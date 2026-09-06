package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_library")
load("@rules_cc//cc:cc_shared_library.bzl", "cc_shared_library")

cc_library(
    name = "fly_task",
    srcs = glob(["*.cpp"]),
    hdrs = glob(["*.h"]),
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
    linkstatic = False,
    deps = [
        "//src/container/cpp:fly_container_aliases",
        "//src/log/cpp:fly_log",
        "//src/common/serialization:fly_serialization",
    ],
)


cc_shared_library(
    name = "fly_task_so",
    deps = [":fly_task"],
    dynamic_deps = [
        "//src/log/cpp:fly_log_so",
        "//src/common/serialization:fly_serialization_so",
    ],
    visibility = ["//visibility:public"],
)
