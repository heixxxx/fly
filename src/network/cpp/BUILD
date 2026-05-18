package(default_visibility = ["//visibility:public"])

cc_library(
    name = "fly_network_transport",
    hdrs = ["transport.h", "tcp_transport.h"],
    srcs = ["tcp_transport.cpp"],
    deps = [
        "//src/common/cpp:fly_common_types",
        "//src/serialization/cpp:fly_serialization_macros",
    ],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
)

cc_library(
    name = "fly_network_message_protocol",
    hdrs = ["message_protocol.h"],
    deps = [
        ":fly_network_transport",
        ":fly_network_message_types",
        "//src/common/cpp:fly_common_types",
        "//src/serialization/cpp:fly_serialization_macros",
    ],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
)

cc_library(
    name = "fly_network_message_types",
    hdrs = ["message_types.h"],
    deps = [
        "//src/common/cpp:fly_common_types",
        "//src/serialization/cpp:fly_serialization_macros",
    ],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
)

cc_library(
    name = "fly_network_io_thread_pool",
    hdrs = ["io_thread_pool.h"],
    srcs = ["io_thread_pool.cpp"],
    deps = [
        "//src/common/cpp:fly_common_types",
    ],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
)

cc_library(
    name = "fly_network_data_client",
    hdrs = ["data_client.h"],
    srcs = ["data_client.cpp"],
    deps = [
        ":fly_network_message_protocol",
        ":fly_network_message_types",
        "//src/common/cpp:fly_common_types",
    ],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
)

cc_library(
    name = "fly_network_master_client",
    hdrs = ["master_client.h"],
    srcs = ["master_client.cpp"],
    deps = [
        ":fly_network_message_protocol",
        ":fly_network_message_types",
        "//src/common/cpp:fly_common_types",
    ],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
)

cc_library(
    name = "fly_network_reactor",
    hdrs = ["reactor.h"],
    srcs = ["reactor.cpp"],
    deps = [
        ":fly_network_transport",
        ":fly_network_message_protocol",
        ":fly_network_message_types",
        ":fly_network_io_thread_pool",
    ],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
)

cc_library(
    name = "fly_network_cpp",
    deps = [
        ":fly_network_transport",
        ":fly_network_message_protocol",
        ":fly_network_message_types",
        ":fly_network_io_thread_pool",
        ":fly_network_reactor",
        ":fly_network_data_client",
        ":fly_network_master_client",
    ],
)