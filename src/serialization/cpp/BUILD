package(default_visibility = ["//visibility:public"])

cc_library(
    name = "fly_serialization_macros",
    hdrs = ["serialization_macros.h"],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
    deps = [
        "@bitsery//:bitsery",
        "//src/common/cpp:fly_common_types",
        ":fly_bitsery_ext",
        "//third_party:boost_preprocessor",
    ],
)

cc_library(
    name = "fly_bitsery_ext",
    hdrs = ["bitsery_ext/version.h"],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
    deps = [
        "@bitsery//:bitsery",
    ],
)

cc_library(
    name = "fly_object_header",
    srcs = ["object_header.cpp"],
    hdrs = ["object_header.h"],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
    deps = [
        "//src/common/cpp:fly_common_types",
    ],
)

cc_library(
    name = "fly_compressing_streambuf",
    srcs = ["compressing_streambuf.cpp"],
    hdrs = ["compressing_streambuf.h"],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
    deps = [
        "//src/common/cpp:fly_common_types",
        "//src/storage/cpp:fly_storage_compression_impl",
    ],
)