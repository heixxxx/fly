package(default_visibility = ["//visibility:public"])

cc_test(
    name = "task_executor_test",
    srcs = ["task_executor_test.cpp"],
    deps = [
        "//src/agent/cpp:fly_agent_task_executor",
        "@com_google_googletest//:gtest_main",
    ],
    copts = ["-std=c++20"],
)