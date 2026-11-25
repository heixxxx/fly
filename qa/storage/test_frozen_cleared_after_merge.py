#!/usr/bin/env python3
"""Test: merge 后 frozen_dbs_ 应清除，新 db 复用同 path 不被误判 frozen。

Bug: MasterAgent::frozen_dbs_ 只有 insert 没有 erase。freeze + merge 后
新建同 path db 的 WriteRegister 被 is_db_frozen 误判为 frozen 而 reject。
commit_write 漏处理 WRITE_TO_FROZEN_DB 导致静默忽略（碰巧蒙对结果），
但 -j4 高负载时时序差异导致偶发失败。
"""
import os
import shutil

from test import record, wait_tasks_no_failure
from fly import open_db, merge_db, launch_workers, get_config
from fly.runtime import get_agent

DB_BASE = os.path.join(get_config().get_str("log_dir"), "frozen_merge_db")
MATRIX_PATH = os.path.join(DB_BASE, "matrix")
MERGE_TARGET = os.path.join(DB_BASE, "shared_matrix")

from storage import Database


class MatrixDb(Database):
    role = "matrix"


def cleanup():
    for p in [DB_BASE, MATRIX_PATH, MERGE_TARGET]:
        if os.path.isdir(p):
            shutil.rmtree(p, ignore_errors=True)


def main():
    cleanup()

    master = get_agent()
    launch_workers([{"host": "host_A"}])
    from test import wait_until
    # 保留：等待原语
    assert wait_until(lambda: master.worker_count >= 1, timeout=10), \
        "worker should connect"

    # t0: 建 matrix_db + 写数据 + freeze
    from test import write_data
    matrix_db = open_db(MATRIX_PATH, db_cls=MatrixDb, logical_name="matrix")
    write_data(matrix_db, "data/original", 100)
    # 保留：等待原语（freeze 前同步点）
    assert master.wait_for_all_tasks(timeout=10) or len(master.completed_tasks) >= 1
    # freeze 前置同步点：写 task 完成落账（替代裸 sleep 缓冲）
    # 保留：等待原语
    assert wait_until(lambda: len(master.completed_tasks) >= 1, timeout=10), \
        "write task must complete before freeze"
    matrix_db.freeze()
    matrix_frozen = matrix_db.is_frozen()
    # 原断言：assert matrix_db.is_frozen()
    record("matrix_frozen", matrix_frozen, detail=matrix_frozen)

    # t1: merge 到 shared_matrix（删源）
    merged_db = merge_db(MATRIX_PATH, merge_db_path=MERGE_TARGET, delete_source=True)
    source_deleted = not os.path.isdir(MATRIX_PATH)
    # 原断言：assert not os.path.isdir(MATRIX_PATH), "source should be deleted"
    record("source_deleted", source_deleted, detail=source_deleted)

    # t2: 在原址新建 db，写数据——不应被 frozen reject
    matrix_db_B = open_db(MATRIX_PATH, db_cls=MatrixDb, logical_name="matrix")
    write_data(matrix_db_B, "data/new", 200)

    # task 应成功完成（不应因 frozen reject 失败）
    # 原断言：assert master.wait_for_all_tasks(timeout=10), \
    # 原断言：assert len(master.failed_tasks) == 0, \
    assert wait_tasks_no_failure(master, 2, "no_failed_tasks", timeout=10.0), \
        "write to new db after merge should succeed, not be rejected as frozen"

    from _fly_log import INFO
    INFO("[DONE] test_frozen_cleared_after_merge")


if __name__ == "__main__":
    main()
