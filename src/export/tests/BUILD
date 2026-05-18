package(default_visibility = ["//visibility:public"])

cc_test(
    name = "export_test",
    srcs = ["export_test.cpp"],
    deps = [
        "@com_google_googletest//:gtest_main",
        "//src/export/cpp:fly_export_macros",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "export_functional_test",
    srcs = ["export_functional_test.cpp"],
    deps = [
        "@com_google_googletest//:gtest_main",
        "//src/export/cpp:fly_export_macros",
        "//src/serialization/cpp:fly_serialization",
    ],
    copts = ["-std=c++20"],
)
