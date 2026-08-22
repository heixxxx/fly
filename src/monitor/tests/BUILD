package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_test")
load("@rules_python//python:defs.bzl", "py_test")

cc_test(
    name = "metrics_db_test",
    srcs = ["metrics_db_test.cc"],
    deps = [
        "@googletest//:gtest_main",
        "//src/monitor/cpp:fly_monitor",
        "//third_party/sqlite:sqlite3",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "monitor_sampler_test",
    srcs = ["monitor_sampler_test.cc"],
    deps = [
        "@googletest//:gtest_main",
        "//src/monitor/cpp:fly_monitor",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "task_resource_tracker_test",
    srcs = ["task_resource_tracker_test.cc"],
    deps = [
        "@googletest//:gtest_main",
        "//src/monitor/cpp:fly_monitor",
    ],
    copts = ["-std=c++20"],
)

py_test(
    name = "task_io_test",
    srcs = ["task_io_test.py"],
    main = "task_io_test.py",
    deps = ["//src/monitor:monitor_pkg"],
    imports = ["../.."],
)
