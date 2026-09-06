# common/concurrent 测试

package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_test")

cc_test(
    name = "concurrent_map_test",
    srcs = ["concurrent_map_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/common/concurrent:fly_common_concurrent",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "concurrent_queue_test",
    srcs = ["concurrent_queue_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/common/concurrent:fly_common_concurrent",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "writer_pref_rwlock_test",
    srcs = ["writer_pref_rwlock_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/common/concurrent:fly_common_concurrent",
    ],
    copts = ["-std=c++20"],
)
