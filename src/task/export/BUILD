package(default_visibility = ["//visibility:public"])

cc_binary(
    name = "_fly_task.so",
    srcs = ["task_export.cpp"],
    deps = [
        "@nanobind//:nanobind_src",
        "//src/task/cpp:fly_task_cpp",
        "//src/export/cpp:fly_export_macros",
    ],
    copts = [
        "-std=c++20",
        "-I/usr/local/include/python3.12",
    ],
    linkopts = [
        "-L/usr/local/lib",
        "-lpython3.12",
        "-lpthread",
    ],
    linkshared = True,
    linkstatic = True,
)