package(default_visibility = ["//visibility:public"])

cc_library(
    name = "fly_network",
    srcs = [
        "tcp_transport.cpp",
        "io_thread_pool.cpp",
        "data_client.cpp",
        "master_client.cpp",
        "reactor.cpp",
    ],
    hdrs = [
        "data_client.h",
        "io_thread_pool.h",
        "master_client.h",
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
