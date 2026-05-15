workspace(name = "fly")

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

# robin_map - hash map used by nanobind (need version >= 1.3.0, < 2.0.0)
http_archive(
    name = "robin_map",
    strip_prefix = "robin-map-1.3.0",
    urls = ["https://github.com/Tessil/robin-map/archive/refs/tags/v1.3.0.tar.gz"],
    build_file = "@//third_party:robin_map.BUILD",
)

# nanobind - thin bindings between C++ and Python
http_archive(
    name = "nanobind",
    strip_prefix = "nanobind-2.12.0",
    urls = ["https://github.com/wjakob/nanobind/archive/refs/tags/v2.12.0.tar.gz"],
    build_file = "@//third_party:nanobind.BUILD",
)

# bitsery - header-only C++ serialization library with versioning support
http_archive(
    name = "bitsery",
    strip_prefix = "bitsery-5.2.4",
    urls = ["https://github.com/fraillt/bitsery/archive/refs/tags/v5.2.4.tar.gz"],
    build_file = "@//third_party:bitsery.BUILD",
)

# Google Test
http_archive(
    name = "com_google_googletest",
    strip_prefix = "googletest-1.14.0",
    urls = ["https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz"],
)

# Hedron's bazel-compile-commands-extractor for clangd LSP support
http_archive(
    name = "hedron_compile_commands",
    strip_prefix = "bazel-compile-commands-extractor-main",
    urls = ["https://github.com/hedronvision/bazel-compile-commands-extractor/archive/refs/heads/main.tar.gz"],
)

load("@hedron_compile_commands//:workspace_setup.bzl", "hedron_compile_commands_setup")
hedron_compile_commands_setup()


# Optional: Force clang over gcc
# build --action_env=CC=clang++ (uncomment to use clang explicitly)
# build --action_env=CXX=clang++ (alternative)

# Compilers to check (in order of preference)
# build --action_env=CC=clang++ 17
# build --action_env=CC=clang++ 18