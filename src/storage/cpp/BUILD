# Fly storage module C++ libraries

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "fly_storage_compressor",
    hdrs = ["compressor.h"],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
    deps = [
        "//src/common/cpp:fly_common_types",
    ],
)

cc_library(
    name = "fly_storage_compression_impl",
    srcs = ["compressor.cpp", "lz4_compressor.cpp", "zlib_compressor.cpp", "zstd_compressor.cpp", "compression_utils.cpp"],
    hdrs = ["compressor.h", "lz4_compressor.h", "zlib_compressor.h", "zstd_compressor.h", "compression_utils.h"],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
    deps = [
        ":fly_storage_compressor",
        "//src/common/cpp:fly_common_types",
        "//third_party:lz4",
        "//third_party:zlib",
        "//third_party:zstd",
    ],
)

cc_library(
    name = "fly_storage_object",
    hdrs = ["object.h"],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
    deps = [
        "//src/common/cpp:fly_common_types",
    ],
)

cc_library(
    name = "fly_storage_index_entry",
    hdrs = ["index_entry.h"],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
    deps = [
        "//src/common/cpp:fly_common_types",
        "//src/serialization/cpp:fly_serialization_macros",
    ],
)

cc_library(
    name = "fly_storage_local_index",
    srcs = ["local_index.cpp"],
    hdrs = ["local_index.h"],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
    deps = [
        ":fly_storage_index_entry",
        "//src/common/cpp:fly_common_types",
        "//src/serialization/cpp:fly_serialization_macros",
    ],
)

cc_library(
    name = "fly_storage_db_meta",
    hdrs = ["db_meta.h"],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
    deps = [
        "//src/common/cpp:fly_common_types",
        "//src/serialization/cpp:fly_serialization_macros",
        ":fly_storage_index_entry",
    ],
)

cc_library(
    name = "fly_storage_data_service",
    srcs = ["data_service.cpp"],
    hdrs = ["data_service.h"],
    copts = ["-std=c++20"],
    deps = [
        ":fly_storage_data_reader",
        ":fly_storage_index_entry",
        "//src/common/cpp:fly_common_types",
        "//src/log/cpp:fly_log",
        "//src/network/cpp:fly_network_io_thread_pool",
    ],
    strip_include_prefix = "/src",
)

cc_library(
    name = "fly_storage_data_writer",
    srcs = ["data_writer.cpp"],
    hdrs = ["data_writer.h"],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
    deps = [
        ":fly_storage_local_index",
        ":fly_storage_index_entry",
        ":fly_storage_compression_impl",
        "//src/common/cpp:fly_common_types",
        "//src/serialization/cpp:fly_serialization_macros",
        "//src/serialization/cpp:fly_object_header",
        "//src/serialization/cpp:fly_compressing_streambuf",
    ],
)

cc_library(
    name = "fly_storage_data_reader",
    srcs = ["data_reader.cpp"],
    hdrs = ["data_reader.h"],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
    deps = [
        ":fly_storage_local_index",
        ":fly_storage_index_entry",
        ":fly_storage_compression_impl",
        "//src/common/cpp:fly_common_types",
        "//src/serialization/cpp:fly_serialization_macros",
        "//src/serialization/cpp:fly_object_header",
    ],
)

cc_library(
    name = "fly_storage_database",
    srcs = ["database.cpp"],
    hdrs = ["database.h"],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
    deps = [
        ":fly_storage_data_writer",
        ":fly_storage_data_reader",
        ":fly_storage_data_service",
        ":fly_storage_db_meta",
        ":fly_storage_object",
        ":fly_storage_compression_impl",
        "//src/common/cpp:fly_common_types",
        "//src/serialization/cpp:fly_serialization_macros",
        "//src/core/cpp:fly_core_cpp",
        "//src/agent/cpp:fly_agent_worker_context",
    ],
)

cc_library(
    name = "fly_storage_storage_manager",
    srcs = ["storage_manager.cpp"],
    hdrs = ["storage_manager.h"],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
    deps = [
        ":fly_storage_database",
        ":fly_storage_data_writer",
        "//src/common/cpp:fly_common_types",
    ],
)