load("@rules_cc//cc:defs.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

# System compression libraries - headers in /usr/include, libs via linkopts
cc_library(
    name = "lz4",
    hdrs = [],  
    includes = ["/usr/include"],
    linkopts = ["-llz4"],
)

cc_library(
    name = "zlib",
    hdrs = [],
    includes = ["/usr/include"],
    linkopts = ["-lz"],
)

cc_library(
    name = "zstd",
    hdrs = [],
    includes = ["/usr/include"],
    linkopts = ["-lzstd"],
)

# ISA-L — data_checksum 的 CRC-64 实现（PCLMUL 多态分派，实测 14.6 GB/s）。
# 部署依赖 +libisal2；<isa-l/crc64.h> 只允许出现在 src/common/cpp/data_checksum.cpp。
cc_library(
    name = "isal",
    hdrs = [],
    includes = ["/usr/include"],
    linkopts = ["-lisal"],
)

# Boost Preprocessor (header-only, system library)
cc_library(
    name = "boost_preprocessor",
    hdrs = [],
    includes = ["/usr/include"],
)