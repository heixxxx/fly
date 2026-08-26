# Fly storage module tests

package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_test")
load("@rules_python//python:defs.bzl", "py_test")

cc_test(
    name = "object_cache_test",
    srcs = ["object_cache_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/storage/cpp:fly_storage",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "compressor_test",
    srcs = ["compressor_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/storage/cpp:fly_storage",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "storage_basics_test",
    srcs = ["storage_basics_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/storage/cpp:fly_storage",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "local_index_test",
    srcs = ["local_index_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/storage/cpp:fly_storage",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "data_writer_test",
    srcs = ["data_writer_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/storage/cpp:fly_storage",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "data_reader_test",
    srcs = ["data_reader_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/storage/cpp:fly_storage",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "database_test",
    srcs = ["database_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/storage/cpp:fly_storage",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "storage_manager_test",
    srcs = ["storage_manager_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/storage/cpp:fly_storage",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "data_service_test",
    srcs = ["data_service_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/storage/cpp:fly_storage",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "write_registration_test",
    srcs = ["write_registration_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/storage/cpp:fly_storage",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "write_back_queue_test",
    srcs = ["write_back_queue_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/storage/cpp:fly_storage",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "write_context_hash_test",
    srcs = ["write_context_hash_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/storage/cpp:fly_storage",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "compressing_streambuf_test",
    srcs = ["compressing_streambuf_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/storage/cpp:fly_storage",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "temp_store_test",
    srcs = ["temp_store_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/storage/cpp:fly_storage",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "fly_stream_test",
    srcs = ["fly_stream_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/storage/cpp:fly_storage",
    ],
    copts = ["-std=c++20"],
)

# 并发 micro-benchmark：测量 DataService 索引访问的锁并发性。独立 target，
# 不进常规 test 套件（按需运行测吞吐，输出 ops/sec 对比数据）。运行：
#   ./fly.sh test //src/storage/tests:data_service_concurrency_bench
cc_test(
    name = "data_service_concurrency_bench",
    srcs = ["data_service_concurrency_bench.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/storage/cpp:fly_storage",
    ],
    copts = ["-std=c++20"],
)

# db_meta 纯 Python 测试（不依赖 C++ runtime，stub _fly_log）
py_test(
    name = "db_meta_test",
    srcs = ["test_db_meta.py"],
    main = "test_db_meta.py",
    deps = ["//src/storage/py:fly_storage_py"],
    imports = ["../py"],
)

# Python ReadCache（low/high 两层 LRU）纯 Python 测试
# （2026-08-16 游离测试处置：此前不在 BUILD）
py_test(
    name = "read_cache_test",
    srcs = ["test_read_cache.py"],
    main = "test_read_cache.py",
    deps = ["//src/storage/py:fly_storage_py"],
    imports = ["../py"],
)
