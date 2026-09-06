# common/serialization 测试（gtest + py）

package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_test")
load("@rules_python//python:defs.bzl", "py_test")

cc_test(
    name = "serialization_test",
    srcs = ["serialization_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/common/serialization:fly_serialization",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "object_header_test",
    srcs = ["object_header_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/common/serialization:fly_serialization",
    ],
    copts = ["-std=c++20"],
)

py_test(
    name = "setstate_error_test",
    srcs = ["setstate_error_test.py"],
    imports = ["../../../test/export"],
    deps = [
        "//src/test/export:_fly_test.so",
    ],
    main = "setstate_error_test.py",
)
