# emir lib 库 db 的 C++ 核心：LIBLibrary/LIBCell 数据结构 + Liberty 解析适配层

package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_library")
load("@rules_cc//cc:cc_shared_library.bzl", "cc_shared_library")

cc_library(
    name = "fly_emir_lib",
    srcs = [
        "lib_parser.cpp",
        "lib_types.cpp",
    ],
    hdrs = [
        "lib_parser.h",
        "lib_types.h",
    ],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
    deps = [
        "//src/container/cpp:fly_container_aliases",
        "//src/container/cpp:fly_container",
        "//src/emir/lib/third_party/liberty:liberty_parser_c",
        "//src/common/serialization:fly_serialization",
    ],
    visibility = ["//visibility:public"],
)

cc_shared_library(
    name = "fly_emir_lib_so",
    deps = [":fly_emir_lib"],
    exports_filter = [
        "//src/container/cpp:fly_container",
        "//src/common/serialization:fly_serialization",
        "//src/common/buffer:data_checksum",
    ],
    visibility = ["//visibility:public"],
)
