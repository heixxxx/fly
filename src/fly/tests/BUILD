package(default_visibility = ["//visibility:public"])

load("@rules_python//python:defs.bzl", "py_library", "py_test")
load("@pip//:requirements.bzl", "requirement")

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
        requirement("coverage"),  # 测量自启（sitecustomize→coverage.process_startup）
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
        requirement("coverage"),  # 测量自启（sitecustomize→coverage.process_startup）
        "//src/fly:fly_py",
    ],
    imports = [
        "../core/export",
        "../log/export",
    ],
)

py_test(
    name = "user_task_test",
    srcs = ["test_user_task.py"],
    main = "test_user_task.py",
    deps = [
        requirement("coverage"),  # 测量自启（sitecustomize→coverage.process_startup）
        "//src/fly:fly_py",
        "//src/storage/export:_fly_storage.so",
        "//src/agent/export:_fly_agent.so",
        ":test_executor_tasks",
        requirement("cloudpickle"),  # test calls cloudpickle.dumps directly
    ],
    imports = [
        ".",
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
    name = "userdoc_test",
    srcs = ["test_userdoc.py"],
    main = "test_userdoc.py",
    deps = [
        requirement("coverage"),  # 测量自启（sitecustomize→coverage.process_startup）
        "//src/fly:fly_py",
    ],
)

py_test(
    name = "bootstrap_test",
    srcs = ["test_bootstrap.py"],
    main = "test_bootstrap.py",
    deps = [
        requirement("coverage"),  # 测量自启（sitecustomize→coverage.process_startup）
        "//src/fly:fly_py",
    ],
)

py_test(
    name = "fly_api_test",
    srcs = ["test_fly_api.py"],
    main = "test_fly_api.py",
    deps = [
        requirement("coverage"),  # 测量自启（sitecustomize→coverage.process_startup）
        "//src/fly:fly_py",
    ],
)
