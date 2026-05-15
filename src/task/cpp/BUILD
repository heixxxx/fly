package(default_visibility = ["//visibility:public"])

cc_library(
    name = "fly_task_dependency_graph",
    hdrs = ["dependency_graph.h"],
    srcs = ["dependency_graph.cpp"],
    deps = [
        "//src/common/cpp:fly_common_types",
    ],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
)

cc_library(
    name = "fly_task_cpp",
    deps = [
        ":fly_task_dependency_graph",
    ],
)