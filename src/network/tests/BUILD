# Fly network tests

package(default_visibility = ["//visibility:public"])

cc_test(
    name = "tcp_transport_test",
    srcs = ["tcp_transport_test.cpp"],
    deps = [
        "@com_google_googletest//:gtest_main",
        "//src/network/cpp:fly_network",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "message_protocol_test",
    srcs = ["message_protocol_test.cpp"],
    deps = [
        "@com_google_googletest//:gtest_main",
        "//src/network/cpp:fly_network",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "io_thread_pool_test",
    srcs = ["io_thread_pool_test.cpp"],
    deps = [
        "@com_google_googletest//:gtest_main",
        "//src/network/cpp:fly_network",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "reactor_test",
    srcs = ["reactor_test.cpp"],
    deps = [
        "@com_google_googletest//:gtest_main",
        "//src/network/cpp:fly_network",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "network_integration_test",
    srcs = ["network_integration_test.cpp"],
    deps = [
        "@com_google_googletest//:gtest_main",
        "//src/network/cpp:fly_network",
    ],
    copts = ["-std=c++20"],
)

py_test(
    name = "network_test",
    srcs = ["network_test.py"],
    deps = [
        "//src/network/export:_fly_network.so",
    ],
    main = "network_test.py",
)

cc_test(
    name = "data_transfer_test",
    srcs = ["data_transfer_test.cpp"],
    deps = [
        "@com_google_googletest//:gtest_main",
        "//src/network/cpp:fly_network",
        "//src/storage/cpp:fly_storage",
    ],
    copts = ["-std=c++20"],
)
