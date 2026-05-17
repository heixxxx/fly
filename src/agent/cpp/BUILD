package(default_visibility = ["//visibility:public"])

cc_library(
    name = "fly_agent_task_executor",
    hdrs = ["task_executor.h"],
    srcs = ["task_executor.cpp"],
    deps = [
        "//src/common/cpp:fly_common_types",
    ],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
)

cc_library(
    name = "fly_agent_master",
    hdrs = ["master_agent.h"],
    srcs = ["master_agent.cpp"],
    deps = [
        ":fly_agent_task_executor",
        "//src/common/cpp:fly_common_types",
        "//src/network/cpp:fly_network_reactor",
        "//src/network/cpp:fly_network_transport",
        "//src/task/cpp:fly_task_cpp",
        "//src/log/cpp:fly_log",
        "//src/storage/cpp:fly_storage_database",
    ],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
)

cc_library(
    name = "fly_agent_worker_context",
    hdrs = ["worker_context.h"],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
    deps = [
        "//src/common/cpp:fly_common_types",
    ],
)

cc_library(
    name = "fly_agent_worker",
    hdrs = ["worker_agent.h"],
    srcs = ["worker_agent.cpp"],
    deps = [
        ":fly_agent_task_executor",
        ":fly_agent_worker_context",
        "//src/common/cpp:fly_common_types",
        "//src/network/cpp:fly_network_reactor",
        "//src/network/cpp:fly_network_transport",
        "//src/log/cpp:fly_log",
        "//src/storage/cpp:fly_storage_database",
    ],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
)

cc_library(
    name = "fly_agent_cpp",
    deps = [
        ":fly_agent_task_executor",
        ":fly_agent_master",
        ":fly_agent_worker",
    ],
)