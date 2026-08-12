"""E2E test: load_db — Phase 1: _DB_META incremental format (standalone, 进程内 C++)。

场景2 (two_processes) / 场景3 (moved_db) 已拆为 .pyt 编排：
  test_load_db_two_processes.pyt / test_load_db_moved.pyt
本文件只保留进程内 _DB_META 格式验证（直接用 _fly_storage C++ 扩展，不经 fly subprocess，
必须在 fly 进程跑——.pyt 在 runqa python 进程无 fly env，无法 import _fly_storage）。
"""
import os
import sys
import shutil
import time


def cleanup(path):
    if os.path.isdir(path):
        shutil.rmtree(path, ignore_errors=True)


# ── Phase 1: Standalone _DB_META format test (no subprocess) ──

def test_db_meta_incremental_format():
    """Verify _DB_META incremental format via direct C++ API."""
    db_path = "/tmp/fly_e2e_load_db_p1"
    log_dir = "/tmp/fly_e2e_load_db_p1_logs"

    for p in [db_path, log_dir]:
        cleanup(p)

    import _fly_log as log
    import _fly_storage as storage

    log.init_log(log_dir, 0)

    try:
        sm = storage.ex_stg_get_storage_manager()
        ds = storage.ex_stg_get_data_service()

        db = sm.get_or_create_database(db_path)
        meta_path = os.path.join(db_path, "_DB_META")
        assert os.path.isfile(meta_path), "_DB_META should exist after construction"

        db.write_object_raw("obj/a", "data_a")
        db.write_object_raw("obj/b", "data_b")
        ds.drain_write_back()
        time.sleep(0.3)

        assert db.read_object_raw("obj/a") == "data_a"
        assert db.read_object_raw("obj/b") == "data_b"

        meta = db.load_meta()
        assert meta is not None
        # db_path 字段已删，只验证 created_at + workers
        assert meta.created_at > 0
        assert len(meta.workers) == 0

        db.freeze()
        assert db.is_frozen()
        assert os.path.isfile(os.path.join(db_path, "_FROZEN"))

        sm.close_all()
        print("[PASS] test_db_meta_incremental_format", file=sys.stderr)

    finally:
        log.shutdown_log()
        for p in [db_path, log_dir]:
            cleanup(p)


test_db_meta_incremental_format()
print("\nAll load_db Phase 1 (standalone) tests passed!")
