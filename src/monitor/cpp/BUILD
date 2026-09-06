package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_library")
load("@rules_cc//cc:cc_shared_library.bzl", "cc_shared_library")

# cluster monitor 落盘层：MetricsDb（master 单写 SQLite）+ 采样器。
# GUI（py/serve.py）与 C++ 写侧共用同一 monitor.db 文件（GUI 只读连接）。
cc_library(
    name = "fly_monitor",
    srcs = glob(["*.cpp"]),
    hdrs = glob(["*.h"]),
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
    linkstatic = False,
    linkopts = ["-lpthread"],
    deps = [
        "//src/container/cpp:fly_container_aliases",
        "//src/core/cpp:fly_core",
        "//src/log/cpp:fly_log",
        "//src/network/cpp:fly_network",
        "//third_party/sqlite:sqlite3",
    ],
)

cc_shared_library(
    name = "fly_monitor_so",
    deps = [":fly_monitor"],
    dynamic_deps = [
        "//src/core/cpp:fly_core_so",
        "//src/log/cpp:fly_log_so",
        "//src/network/cpp:fly_network_so",
    ],
    visibility = ["//visibility:public"],
)
