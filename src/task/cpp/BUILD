package(default_visibility = ["//visibility:public"])

cc_library(
    name = "fly_task",
    srcs = [
        "dependency_graph.cpp",
        "worker_manager.cpp",
        "task_scheduler.cpp",
        "metadata_manager.cpp",
        "heartbeat_monitor.cpp",
    ],
    hdrs = [
        "dependency_graph.h",
        "heartbeat_monitor.h",
        "metadata_manager.h",
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
