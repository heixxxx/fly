"""E2E test: load_db — Phase 1: _DB_META JSON format (standalone, 进程内)。

场景2 (two_processes) / 场景3 (moved_db) 已拆为 .pyt 编排：
  test_load_db_two_processes.pyt / test_load_db_moved.pyt
本文件只保留进程内 _DB_META 格式验证（不经 fly subprocess，必须在 fly 进程
跑——.pyt 在 runqa python 进程无 fly env，无法 import _fly_storage）。

_DB_META 已合并为 JSON（version 2，读写权威在 Python 编排层 DbMetaFile）：
本 case 改经 fly.open_db 建库（触发 _init_chain 初写 meta）+ Database.load_meta
（Python 实现，组装 EXStgDbMeta 兼容对象）验证。
"""
import os
import sys
import shutil
import time

from test import qa_tmp


def cleanup(path):
    if os.path.isdir(path):
        shutil.rmtree(path, ignore_errors=True)


# ── Phase 1: Standalone _DB_META JSON format test (no subprocess) ──

def test_db_meta_json_format():
    """Verify _DB_META JSON via the Python orchestration layer."""
    db_path = qa_tmp("fly_e2e_load_db_p1")
    log_dir = qa_tmp("fly_e2e_load_db_p1_logs")

    for p in [db_path, log_dir]:
        cleanup(p)

    import _fly_log as log
    import _fly_storage as storage

    log.init_log(log_dir, 0)

    try:
        from fly import open_db

        db = open_db(db_path)
        meta_path = os.path.join(db_path, "_DB_META")
        assert os.path.isfile(meta_path), "_DB_META should exist after open_db"

        db.write_object("obj/a", "data_a")
        db.write_object("obj/b", "data_b")
        storage.ex_stg_get_data_service().drain_write_back()
        time.sleep(0.3)

        assert db.read_object("obj/a") == "data_a"
        assert db.read_object("obj/b") == "data_b"

        # _DB_META JSON 直读 + Database.load_meta 兼容层双验证。
        import json
        with open(meta_path, encoding="utf-8") as f:
            raw = json.load(f)
        assert raw["created_at"] > 0
        assert raw["version"] == 2
        assert raw["uid"] is not None

        meta = db.load_meta()
        assert meta is not None
        assert meta.created_at > 0
        # master 自写在非 stream 模式才登记 worker；本 case 只验证格式契约，
        # workers 可为空或含 master 登记，不做数量断言。

        db.freeze()
        assert db.is_frozen()
        assert os.path.isfile(os.path.join(db_path, "_FROZEN"))

        storage.ex_stg_get_storage_manager().close_all()
        print("[PASS] test_db_meta_json_format", file=sys.stderr)

    finally:
        log.shutdown_log()
        for p in [db_path, log_dir]:
            cleanup(p)


test_db_meta_json_format()
print("\nAll load_db Phase 1 (standalone) tests passed!")
