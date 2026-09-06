package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_library")
load("@rules_cc//cc:cc_shared_library.bzl", "cc_shared_library")

cc_library(
    name = "fly_storage",
    srcs = glob(["*.cpp"]),
    hdrs = glob(["*.h"]),
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
    linkstatic = False,
    linkopts = ["-llz4", "-lz", "-lzstd"],
    deps = [
        "//src/common/buffer:fly_common_buffer",
        "//src/common/concurrent:fly_common_concurrent",
        "//src/common/io:fly_common_io",
        "//src/common/runtime:fly_common_runtime",
        "//src/container/cpp:fly_container_aliases",
        "//src/common/buffer:data_checksum",
        "//src/common/serialization:fly_serialization",
        "//src/core/cpp:fly_core",
        "//src/log/cpp:fly_log",
        "//src/network/cpp:fly_network",
        "//third_party:lz4",
        "//third_party:zlib",
        "//third_party:zstd",
    ],
)


cc_shared_library(
    name = "fly_storage_so",
    deps = [
        ":fly_storage",
        "//third_party:lz4",
        "//third_party:zlib",
        "//third_party:zstd",
    ],
    dynamic_deps = [
        "//src/common/buffer:data_checksum_so",
        "//src/common/serialization:fly_serialization_so",
        "//src/core/cpp:fly_core_so",
        "//src/log/cpp:fly_log_so",
        "//src/network/cpp:fly_network_so",
    ],
    visibility = ["//visibility:public"],
)