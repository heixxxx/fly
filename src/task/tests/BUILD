package(default_visibility = ["//visibility:public"])

cc_test(
    name = "dependency_graph_test",
    srcs = ["dependency_graph_test.cpp"],
    deps = [
        "//src/task/cpp:fly_task_dependency_graph",
        "@com_google_googletest//:gtest_main",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "worker_manager_test",
    srcs = ["worker_manager_test.cpp"],
    deps = [
        "//src/task/cpp:fly_task_worker_manager",
        "@com_google_googletest//:gtest_main",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "task_scheduler_test",
    srcs = ["task_scheduler_test.cpp"],
    deps = [
        "//src/task/cpp:fly_task_task_scheduler",
        "@com_google_googletest//:gtest_main",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "metadata_manager_test",
    srcs = ["metadata_manager_test.cpp"],
    deps = [
        "//src/task/cpp:fly_task_metadata_manager",
        "@com_google_googletest//:gtest_main",
    ],
    copts = ["-std=c++20"],
)