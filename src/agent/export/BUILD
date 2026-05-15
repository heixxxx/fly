package(default_visibility = ["//visibility:public"])

cc_binary(
    name = "_fly_agent.so",
    srcs = ["agent_export.cpp"],
    deps = [
        "@nanobind//:nanobind_src",
        "//src/agent/cpp:fly_agent_cpp",
        "//src/export/cpp:fly_export_macros",
    ],
    copts = [
        "-std=c++20",
        "-I/usr/include/python3.10",
    ],
    linkopts = [
        "-lpython3.10",
        "-lpthread",
    ],
    linkshared = True,
    linkstatic = True,
)