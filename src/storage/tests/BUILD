# Fly storage module tests

package(default_visibility = ["//visibility:public"])

cc_test(
    name = "compressor_test",
    srcs = ["compressor_test.cpp"],
    deps = [
        "@com_google_googletest//:gtest_main",
        "//src/storage/cpp:fly_storage_compression_impl",
        "//src/common/cpp:fly_common_types",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "storage_basics_test",
    srcs = ["storage_basics_test.cpp"],
    deps = [
        "@com_google_googletest//:gtest_main",
        "//src/storage/cpp:fly_storage_object",
        "//src/storage/cpp:fly_storage_index_entry",
        "//src/storage/cpp:fly_storage_db_meta",
        "//src/common/cpp:fly_common_types",
        "//src/serialization/cpp:fly_serialization_macros",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "local_index_test",
    srcs = ["local_index_test.cpp"],
    deps = [
        "@com_google_googletest//:gtest_main",
        "//src/storage/cpp:fly_storage_local_index",
        "//src/common/cpp:fly_common_types",
        "//src/serialization/cpp:fly_serialization_macros",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "data_writer_test",
    srcs = ["data_writer_test.cpp"],
    deps = [
        "@com_google_googletest//:gtest_main",
        "//src/storage/cpp:fly_storage_data_writer",
        "//src/common/cpp:fly_common_types",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "data_reader_test",
    srcs = ["data_reader_test.cpp"],
    deps = [
        "@com_google_googletest//:gtest_main",
        "//src/storage/cpp:fly_storage_data_reader",
        "//src/storage/cpp:fly_storage_data_writer",
        "//src/common/cpp:fly_common_types",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "database_test",
    srcs = ["database_test.cpp"],
    deps = [
        "@com_google_googletest//:gtest_main",
        "//src/storage/cpp:fly_storage_database",
        "//src/agent/cpp:fly_agent_worker_context",
        "//src/common/cpp:fly_common_types",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "storage_manager_test",
    srcs = ["storage_manager_test.cpp"],
    deps = [
        "@com_google_googletest//:gtest_main",
        "//src/storage/cpp:fly_storage_storage_manager",
        "//src/common/cpp:fly_common_types",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "data_service_test",
    srcs = ["data_service_test.cpp"],
    deps = [
        "@com_google_googletest//:gtest_main",
        "//src/storage/cpp:fly_storage_data_service",
        "//src/storage/cpp:fly_storage_database",
        "//src/common/cpp:fly_common_types",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "write_registration_test",
    srcs = ["write_registration_test.cpp"],
    deps = [
        "@com_google_googletest//:gtest_main",
        "//src/storage/cpp:fly_storage_data_service",
        "//src/storage/cpp:fly_storage_database",
        "//src/agent/cpp:fly_agent_worker_context",
        "//src/common/cpp:fly_common_types",
    ],
    copts = ["-std=c++20"],
)