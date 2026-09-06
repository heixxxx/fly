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
    name = "run_metrics_test",
    srcs = ["run_metrics_test.cpp"],
    deps = [
        "//src/common/testing:fly_common_testing",
        "@googletest//:gtest_main",
        "//src/agent/cpp:fly_agent",
        "@fmt//:fmt",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "master_agent_test",
    srcs = ["master_agent_test.cpp", "test_log_isolation.h"],
    deps = [
        "//src/common/testing:fly_common_testing",
        "@googletest//:gtest_main",
        "//src/agent/cpp:fly_agent_test_hooks",
        "//third_party/sqlite:sqlite3",
    ],
    copts = ["-std=c++20"],
    defines = ["FLY_ENABLE_TEST_HOOKS"],
)

cc_test(
    name = "pending_rpc_map_test",
    srcs = ["pending_rpc_map_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/agent/cpp:fly_agent_test_hooks",
    ],
    copts = ["-std=c++20"],
    defines = ["FLY_ENABLE_TEST_HOOKS"],
)

cc_test(
    name = "worker_agent_test",
    srcs = ["worker_agent_test.cpp", "test_log_isolation.h"],
    deps = [
        "//src/common/testing:fly_common_testing",
        "@googletest//:gtest_main",
        "//src/agent/cpp:fly_agent_test_hooks",
    ],
    copts = ["-std=c++20"],
    defines = ["FLY_ENABLE_TEST_HOOKS"],
)

cc_test(
    name = "agent_integration_test",
    srcs = ["agent_integration_test.cpp", "test_log_isolation.h"],
    deps = [
        "@googletest//:gtest_main",
        "//src/agent/cpp:fly_agent",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "agent_network_test",
    srcs = ["agent_network_test.cpp", "test_log_isolation.h"],
    deps = [
        "@googletest//:gtest_main",
        "//src/agent/cpp:fly_agent_test_hooks",
        "//src/common/testing:fly_common_testing",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "failed_tasks_test",
    srcs = ["failed_tasks_test.cpp"],
    deps = [
        "//src/common/testing:fly_common_testing",
        "@googletest//:gtest_main",
        "//src/agent/cpp:fly_agent",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "write_register_network_test",
    srcs = ["write_register_network_test.cpp", "test_log_isolation.h"],
    deps = [
        "//src/common/testing:fly_common_testing",
        "@googletest//:gtest_main",
        "//src/agent/cpp:fly_agent",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "peer_rpc_server_test",
    srcs = ["peer_rpc_server_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/agent/cpp:fly_agent_test_hooks",
    ],
    copts = ["-std=c++20"],
    defines = ["FLY_ENABLE_TEST_HOOKS"],
)
