package(default_visibility = ["//visibility:public"])

cc_library(
    name = "fly_agent",
    srcs = [
        "task_executor.cpp",
        "master_agent.cpp",
        "worker_agent.cpp",
    ],
    hdrs = [
        "master_agent.h",
        "task_executor.h",
        "worker_agent.h",
    ],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
    linkstatic = False,
    linkopts = ["-lpthread"],
    deps = [
        "//src/common/cpp:fly_common_types",
        "//src/core/cpp:fly_core",
        "//src/log/cpp:fly_log",
        "//src/network/cpp:fly_network",
        "//src/storage/cpp:fly_storage",
        "//src/task/cpp:fly_task",
    ],
)
