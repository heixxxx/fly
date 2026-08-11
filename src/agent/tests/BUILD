# Fly agent tests

package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_test")

cc_test(
    name = "task_executor_test",
    srcs = ["task_executor_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/agent/cpp:fly_agent",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "master_agent_test",
    srcs = ["master_agent_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/agent/cpp:fly_agent_test_hooks",
    ],
    copts = ["-std=c++20"],
    defines = ["FLY_ENABLE_TEST_HOOKS"],
)

cc_test(
    name = "worker_agent_test",
    srcs = ["worker_agent_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/agent/cpp:fly_agent",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "agent_integration_test",
    srcs = ["agent_integration_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/agent/cpp:fly_agent",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "agent_network_test",
    srcs = ["agent_network_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/agent/cpp:fly_agent",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "failed_tasks_test",
    srcs = ["failed_tasks_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/agent/cpp:fly_agent",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "write_register_network_test",
    srcs = ["write_register_network_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/agent/cpp:fly_agent",
    ],
    copts = ["-std=c++20"],
)
