package(default_visibility = ["//visibility:public"])

cc_binary(
    name = "_fly_agent.so",
    srcs = ["agent_export.cpp"],
    deps = [
        "@nanobind//:nanobind_src",
        "//src/export/cpp:fly_export_macros",
        "//src/agent/cpp:fly_agent",
    ],
    copts = ["-std=c++20"],
    linkopts = ["-lpthread"],
    linkshared = True,
)