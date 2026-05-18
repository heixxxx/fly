package(default_visibility = ["//visibility:public"])

cc_binary(
    name = "fly",
    srcs = ["main.cpp"],
    copts = ["-std=c++20"],
    linkopts = ["-lpthread", "-ldl"],
    deps = [
        "//src/core/cpp:fly_core",
        "//src/serialization/cpp:fly_serialization",
        "//src/log/cpp:fly_log",
        "//src/storage/cpp:fly_storage",
        "//src/network/cpp:fly_network",
        "//src/task/cpp:fly_task",
        "//src/agent/cpp:fly_agent",
    ],
    data = [
        "//src/test/export:_fly_test.so",
    ],
)