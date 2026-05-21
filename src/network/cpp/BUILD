package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_library")
load("@rules_cc//cc:cc_shared_library.bzl", "cc_shared_library")

cc_library(
    name = "fly_network",
    srcs = [
        "tcp_transport.cpp",
        "io_thread_pool.cpp",
        "data_client.cpp",
        "metadata_client.cpp",
        "reactor.cpp",
    ],
    hdrs = [
        "data_client.h",
        "io_thread_pool.h",
        "metadata_client.h",
        "message_protocol.h",
        "message_types.h",
        "reactor.h",
        "tcp_transport.h",
        "transport.h",
    ],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
    linkstatic = False,
    linkopts = ["-lpthread"],
    deps = [
        "//src/common/cpp:fly_common_types",
        "//src/serialization/cpp:fly_serialization",
    ],
)


cc_shared_library(
    name = "fly_network_so",
    deps = [":fly_network"],
    dynamic_deps = [
        "//src/serialization/cpp:fly_serialization_so",
    ],
    visibility = ["//visibility:public"],
)