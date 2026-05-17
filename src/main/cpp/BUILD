package(default_visibility = ["//visibility:public"])

cc_binary(
    name = "fly",
    srcs = ["main.cpp"],
    copts = [
        "-std=c++20",
        "-I/usr/include/python3.10",
    ],
    linkopts = [
        "-lpython3.10",
        "-lpthread",
    ],
)
