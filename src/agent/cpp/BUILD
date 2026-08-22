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
        "//src/monitor/cpp:fly_monitor",
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
        "//src/monitor/cpp:fly_monitor_so",
    ],
    visibility = ["//visibility:public"],
)

# fly_agent 的 testonly 变体：定义 FLY_ENABLE_TEST_HOOKS，激活 master_agent 中
# #ifdef FLY_ENABLE_TEST_HOOKS 包裹的测试钩子与 getter。release 路径（fly_agent /
# fly_agent_so / fly 二进制）永不依赖此 target，故钩子代码不进发布产物。
# 仅供 cc_test 静态链接（linkstatic=True），不产出 .so、不参与 export 链。
cc_library(
    name = "fly_agent_test_hooks",
    srcs = glob(["*.cpp"]),
    hdrs = glob(["*.h"]),
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
    defines = ["FLY_ENABLE_TEST_HOOKS"],
    linkstatic = True,
    linkopts = ["-lpthread"],
    testonly = True,
    deps = [
        "//src/common/cpp:fly_common_types",
        "//src/core/cpp:fly_core",
        "//src/log/cpp:fly_log",
        "//src/message/cpp:fly_message",
        "//src/network/cpp:fly_network",
        "//src/serialization/cpp:fly_serialization",
        "//src/storage/cpp:fly_storage",
        "//src/task/cpp:fly_task",
        "//src/monitor/cpp:fly_monitor",
    ],
)