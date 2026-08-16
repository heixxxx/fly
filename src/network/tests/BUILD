# Fly network tests

package(default_visibility = ["//visibility:public"])

load("@rules_python//python:defs.bzl", "py_test")

load("@rules_cc//cc:defs.bzl", "cc_test")

cc_test(
    name = "net_quality_monitor_test",
    srcs = ["net_quality_monitor_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/network/cpp:fly_network",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "tcp_connection_manager_test",
    srcs = ["tcp_connection_manager_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/network/cpp:fly_network",
    ],
    copts = ["-std=c++20"],
    defines = ["FLY_ENABLE_TEST_HOOKS"],  # 暴露 register_connection 供 Problem 6 测试
)

cc_test(
    name = "message_protocol_test",
    srcs = ["message_protocol_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/network/cpp:fly_network",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "reactor_test",
    srcs = ["reactor_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/network/cpp:fly_network",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "network_integration_test",
    srcs = ["network_integration_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/network/cpp:fly_network",
    ],
    copts = ["-std=c++20"],
)

py_test(
    name = "network_test",
    srcs = ["network_test.py"],
    data = [
        "//src/network/export:_fly_network.so",
    ],
    deps = [
        "//src/network/export:_fly_network.so",
    ],
    main = "network_test.py",
)

cc_test(
    name = "data_transfer_test",
    srcs = ["data_transfer_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/network/cpp:fly_network",
        "//src/storage/cpp:fly_storage",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "data_client_pool_test",
    srcs = ["data_client_pool_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/network/cpp:fly_network",
        "//src/storage/cpp:fly_storage",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "metadata_client_test",
    srcs = ["metadata_client_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/network/cpp:fly_network",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "transport_interface_test",
    srcs = ["transport_interface_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/network/cpp:fly_network",
    ],
    copts = ["-std=c++20"],
)
