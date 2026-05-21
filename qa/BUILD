package(default_visibility = ["//visibility:public"])

load("@rules_python//python:defs.bzl", "py_test")

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

py_test(
    name = "distributed_data_flow_test",
    srcs = ["test_distributed_data_flow.py"],
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
    ],
    main = "test_distributed_data_flow.py",
)

py_test(
    name = "full_dependency_chain_test",
    srcs = ["test_full_dependency_chain.py"],
    deps = [
        "//src/storage/py:fly_storage_py",
        "//src/fly:fly_py",
        "//src/task/export:_fly_task.so",
    ],
    imports = [
        "../src/storage/export",
        "../src/core/export",
        "../src/log/export",
        "../src/agent/export",
        "../src/network/export",
        "../src/task/export",
    ],
    main = "test_full_dependency_chain.py",
)

py_test(
    name = "worker_timeout_test",
    srcs = ["test_worker_timeout.py"],
    deps = [
        "//src/storage/py:fly_storage_py",
        "//src/fly:fly_py",
        "//src/task/export:_fly_task.so",
    ],
    imports = [
        "../src/storage/export",
        "../src/core/export",
        "../src/log/export",
        "../src/agent/export",
        "../src/network/export",
        "../src/task/export",
    ],
    main = "test_worker_timeout.py",
)

py_test(
    name = "database_freeze_protection_test",
    srcs = ["test_database_freeze_protection.py"],
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
    ],
    main = "test_database_freeze_protection.py",
)

py_test(
    name = "capability_scheduling_test",
    srcs = ["test_capability_scheduling.py"],
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
    main = "test_capability_scheduling.py",
)

py_test(
    name = "dynamic_property_scheduling_test",
    srcs = ["test_dynamic_property_scheduling.py"],
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
    main = "test_dynamic_property_scheduling.py",
)

py_test(
    name = "no_matching_worker_test",
    srcs = ["test_no_matching_worker.py"],
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
    main = "test_no_matching_worker.py",
)

py_test(
    name = "fail_unscheduleable_test",
    srcs = ["test_fail_unscheduleable.py"],
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
    main = "test_fail_unscheduleable.py",
)