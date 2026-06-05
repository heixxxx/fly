package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_binary")

cc_binary(
    name = "fly",
    srcs = ["main.cpp"],
    copts = ["-std=c++20"],
    linkopts = ["-lpthread", "-ldl"],
    deps = [
        "//src/core/cpp:fly_core",
        "//src/serialization/cpp:fly_serialization",
        "//src/log/cpp:fly_log",
        "//src/storage/cpp:fly_storage",
        "//src/network/cpp:fly_network",
        "//src/task/cpp:fly_task",
        "//src/agent/cpp:fly_agent",
        "//src/solver/cpp:fly_solver",
    ],

    dynamic_deps = [
        "//src/core/cpp:fly_core_so",
        "//src/serialization/cpp:fly_serialization_so",
        "//src/log/cpp:fly_log_so",
        "//src/storage/cpp:fly_storage_so",
        "//src/network/cpp:fly_network_so",
        "//src/task/cpp:fly_task_so",
        "//src/agent/cpp:fly_agent_so",
        "//src/solver/cpp:fly_solver_so",
    ],
    data = [
        "//src/test/export:_fly_test.so",
        "//src/storage/export:_fly_storage.so",
        "//src/network/export:_fly_network.so",
        "//src/agent/export:_fly_agent.so",
        "//src/core/export:_fly_core.so",
        "//src/log/export:_fly_log.so",
        "//src/task/export:_fly_task.so",
        "//src/solver/export:_fly_solver.so",
    ],
)