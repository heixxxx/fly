package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_library")
load("@rules_cc//cc:cc_shared_library.bzl", "cc_shared_library")

cc_library(
    name = "fly_network",
    srcs = glob(["*.cpp"]),
    hdrs = glob(["*.h"]),
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
    linkstatic = False,
    linkopts = ["-lpthread"],
    deps = [
        "//src/common/buffer:data_checksum",
        "//src/common/buffer:fly_common_buffer",
        "//src/common/io:fly_common_io",
        "//src/common/runtime:fly_common_runtime",
        "//src/container/cpp:fly_container_aliases",
        "//src/log/cpp:fly_log",
        "//src/common/serialization:fly_serialization",
    ],
)


cc_shared_library(
    name = "fly_network_so",
    deps = [":fly_network"],
    dynamic_deps = [
        "//src/log/cpp:fly_log_so",
        "//src/common/serialization:fly_serialization_so",
    ],
    visibility = ["//visibility:public"],
)