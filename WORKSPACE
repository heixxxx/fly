workspace(name = "fly")

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

# nanobind - thin bindings between C++ and Python
http_archive(
    name = "nanobind",
    strip_prefix = "nanobind-2.12.0",
    urls = ["https://github.com/wjakob/nanobind/archive/refs/tags/v2.12.0.tar.gz"],
    build_file = "@//third_party:nanobind.BUILD",
)

# zpp_bits - single-header C++20 serialization library
http_archive(
    name = "zpp_bits",
    strip_prefix = "zpp_bits-4.4.12",
    urls = ["https://github.com/eyalz800/zpp_bits/archive/refs/tags/v4.4.12.tar.gz"],
    build_file = "@//third_party:zpp_bits.BUILD",
)

# Google Test
http_archive(
    name = "com_google_googletest",
    strip_prefix = "googletest-1.14.0",
    urls = ["https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz"],
)