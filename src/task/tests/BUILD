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