package(default_visibility = ["//visibility:public"])

load("@rules_python//python:defs.bzl", "py_library", "py_test")

py_library(
    name = "test_executor_tasks",
    srcs = ["test_executor_tasks.py"],
    deps = [
        "//src/log/export:_fly_log.so",
    ],
    imports = ["../log/export"],
)

py_test(
    name = "executor_test",
    srcs = ["test_executor.py"],
    main = "test_executor.py",
    deps = [
        "//src/fly:fly_py",
        "//src/storage/export:_fly_storage.so",
        ":test_executor_tasks",
    ],
    imports = [
        "../storage/export",
        "../storage/py",
        "../agent/export",
        "../agent/py",
        "../log/export",
        "../core/export",
        "../task/export",
        "../task/py",
    ],
)

py_test(
    name = "main_test",
    srcs = ["test_main.py"],
    main = "test_main.py",
    deps = [
        "//src/fly:fly_py",
    ],
    imports = [
        "../core/export",
        "../log/export",
    ],
)
