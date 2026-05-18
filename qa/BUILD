package(default_visibility = ["//visibility:public"])

py_test(
    name = "smoke_test",
    srcs = ["smoke_test.py"],
    deps = [
        "//src/core/py:fly_core_py",
    ],
)

py_test(
    name = "storage_test",
    srcs = ["storage_test.py"],
    deps = [
        "//src/storage/py:fly_storage_py",
        "//src/fly:fly_py",
    ],
    imports = [
        "../src/storage/export",
        "../src/core/export",
        "../src/log/export",
        "../src/agent/export",
        "../src/network/export",
        "../src/task/export",
    ],
)