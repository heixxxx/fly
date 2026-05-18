package(default_visibility = ["//visibility:public"])

cc_binary(
    name = "fly",
    srcs = ["main.cpp"],
    data = [
        "//src/test/export:_fly_test.so",
    ],
    copts = [
        "-std=c++20",
        "-I/usr/include/python3.10",
    ],
    linkopts = [
        "-lpython3.10",
        "-lpthread",
    ],
)
