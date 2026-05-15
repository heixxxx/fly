package(default_visibility = ["//visibility:public"])

cc_test(
    name = "master_agent_test",
    srcs = ["master_agent_test.cpp"],
    deps = [
        "//src/agent/cpp:fly_agent_master",
        "@com_google_googletest//:gtest_main",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "worker_agent_test",
    srcs = ["worker_agent_test.cpp"],
    deps = [
        "//src/agent/cpp:fly_agent_worker",
        "@com_google_googletest//:gtest_main",
    ],
    copts = ["-std=c++20"],
)