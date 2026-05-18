package(default_visibility = ["//visibility:public"])

cc_binary(
    name = "fly",
    srcs = ["main.cpp"],
    data = [
        "//src/test/export:_fly_test.so",
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
)
