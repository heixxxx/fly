# Fly task tests

package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_test")

cc_test(
    name = "dependency_graph_test",
    srcs = ["dependency_graph_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/task/cpp:fly_task",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "worker_manager_test",
    srcs = ["worker_manager_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/task/cpp:fly_task",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "task_scheduler_test",
    srcs = ["task_scheduler_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/task/cpp:fly_task",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "task_manager_test",
    srcs = ["task_manager_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/task/cpp:fly_task",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "heartbeat_monitor_test",
    srcs = ["heartbeat_monitor_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/task/cpp:fly_task",
    ],
    copts = ["-std=c++20"],
)

# 调度热循环 micro-benchmark：测量 schedule_next/get_ready_tasks 的开销。
# 独立 target，不进常规 test 套件（按需运行测吞吐）。运行：
#   ./fly.sh test //src/task/tests:scheduling_hotloop_bench
cc_test(
    name = "scheduling_hotloop_bench",
    srcs = ["scheduling_hotloop_bench.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/task/cpp:fly_task",
    ],
    copts = ["-std=c++20"],
)
