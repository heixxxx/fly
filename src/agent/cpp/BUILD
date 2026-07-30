package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_library")
load("@rules_cc//cc:cc_shared_library.bzl", "cc_shared_library")

cc_library(
    name = "fly_agent",
    srcs = glob(["*.cpp"]),
    hdrs = glob(["*.h"]),
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
    linkstatic = False,
    linkopts = ["-lpthread"],
    deps = [
        "//src/common/cpp:fly_common_types",
        "//src/core/cpp:fly_core",
        "//src/log/cpp:fly_log",
        "//src/message/cpp:fly_message",
        "//src/network/cpp:fly_network",
        "//src/serialization/cpp:fly_serialization",
        "//src/storage/cpp:fly_storage",
        "//src/task/cpp:fly_task",
    ],
)


cc_shared_library(
    name = "fly_agent_so",
    deps = [":fly_agent"],
    dynamic_deps = [
        "//src/core/cpp:fly_core_so",
        "//src/log/cpp:fly_log_so",
        "//src/message/cpp:fly_message_so",
        "//src/network/cpp:fly_network_so",
        "//src/storage/cpp:fly_storage_so",
        "//src/task/cpp:fly_task_so",
    ],
    visibility = ["//visibility:public"],
)