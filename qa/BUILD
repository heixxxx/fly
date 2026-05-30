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

py_test(
    name = "backup_data_test",
    srcs = ["test_backup_data.py"],
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
    main = "test_backup_data.py",
)

py_test(
    name = "complex_scenario_test",
    srcs = ["test_complex_scenario.py"],
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
    main = "test_complex_scenario.py",
)

py_test(
    name = "fanout_write_test",
    srcs = ["test_fanout_write.py"],
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
    main = "test_fanout_write.py",
)

py_test(
    name = "freeze_write_reject_test",
    srcs = ["test_freeze_write_reject.py"],
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
    main = "test_freeze_write_reject.py",
)

py_test(
    name = "frozen_db_write_test",
    srcs = ["test_frozen_db_write.py"],
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
    main = "test_frozen_db_write.py",
)

py_test(
    name = "graceful_shutdown_test",
    srcs = ["test_graceful_shutdown.py"],
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
    main = "test_graceful_shutdown.py",
)

py_test(
    name = "load_balancing_test",
    srcs = ["test_load_balancing.py"],
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
    main = "test_load_balancing.py",
)

py_test(
    name = "load_db_test",
    srcs = ["test_load_db.py"],
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
    main = "test_load_db.py",
)

py_test(
    name = "load_db_invalid_test",
    srcs = ["test_load_db_invalid.py"],
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
    main = "test_load_db_invalid.py",
)

py_test(
    name = "load_frozen_db_write_test",
    srcs = ["test_load_frozen_db_write.py"],
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
    main = "test_load_frozen_db_write.py",
)

py_test(
    name = "open_db_edge_cases_test",
    srcs = ["test_open_db_edge_cases.py"],
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
    main = "test_open_db_edge_cases.py",
)

py_test(
    name = "pending_task_persist_test",
    srcs = ["test_pending_task_persist.py"],
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
    main = "test_pending_task_persist.py",
)

py_test(
    name = "remove_object_test",
    srcs = ["test_remove_object.py"],
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
    main = "test_remove_object.py",
)

py_test(
    name = "restart_failed_tasks_test",
    srcs = ["test_restart_failed_tasks.py"],
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
    main = "test_restart_failed_tasks.py",
)

py_test(
    name = "shutdown_broadcast_test",
    srcs = ["test_shutdown_broadcast.py"],
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
    main = "test_shutdown_broadcast.py",
)

py_test(
    name = "stress_test",
    srcs = ["test_stress.py"],
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
    main = "test_stress.py",
)

py_test(
    name = "stress_concurrent_write_test",
    srcs = ["test_stress_concurrent_write.py"],
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
    main = "test_stress_concurrent_write.py",
)

py_test(
    name = "stress_cross_db_test",
    srcs = ["test_stress_cross_db.py"],
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
    main = "test_stress_cross_db.py",
)

py_test(
    name = "stress_dep_chain_test",
    srcs = ["test_stress_dep_chain.py"],
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
    main = "test_stress_dep_chain.py",
)

py_test(
    name = "stress_freeze_reject_test",
    srcs = ["test_stress_freeze_reject.py"],
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
    main = "test_stress_freeze_reject.py",
)

py_test(
    name = "stress_multi_db_test",
    srcs = ["test_stress_multi_db.py"],
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
    main = "test_stress_multi_db.py",
)

py_test(
    name = "stress_readwrite_test",
    srcs = ["test_stress_readwrite.py"],
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
    main = "test_stress_readwrite.py",
)

py_test(
    name = "stress_stability_test",
    srcs = ["test_stress_stability.py"],
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
    main = "test_stress_stability.py",
)

py_test(
    name = "task_exception_test",
    srcs = ["test_task_exception.py"],
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
    main = "test_task_exception.py",
)

py_test(
    name = "unresolvable_dependency_test",
    srcs = ["test_unresolvable_dependency.py"],
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
    main = "test_unresolvable_dependency.py",
)

py_test(
    name = "wait_obj_test",
    srcs = ["test_wait_obj.py"],
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
    main = "test_wait_obj.py",
)

py_test(
    name = "wait_tasks_timeout_test",
    srcs = ["test_wait_tasks_timeout.py"],
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
    main = "test_wait_tasks_timeout.py",
)

py_test(
    name = "worker_crash_test",
    srcs = ["test_worker_crash.py"],
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
    main = "test_worker_crash.py",
)

py_test(
    name = "write_immediate_dep_test",
    srcs = ["test_write_immediate_dep.py"],
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
    main = "test_write_immediate_dep.py",
)