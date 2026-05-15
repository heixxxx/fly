package(default_visibility = ["//visibility:public"])

cc_library(
    name = "fly_agent_task_executor",
    hdrs = ["task_executor.h"],
    srcs = ["task_executor.cpp"],
    deps = [
        "//src/common/cpp:fly_common_types",
    ],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
)

cc_library(
    name = "fly_agent_cpp",
    deps = [
        ":fly_agent_task_executor",
    ],
)