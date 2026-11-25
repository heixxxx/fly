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

from test import qa_tmp, record


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
        meta_exists = os.path.isfile(meta_path)
        # 原断言：assert os.path.isfile(meta_path), "_DB_META should exist after open_db"
        record("meta_exists", meta_exists, detail=meta_exists)

        db.write_object("obj/a", "data_a")
        db.write_object("obj/b", "data_b")
        storage.ex_stg_get_data_service().drain_write_back()
        time.sleep(0.3)

        read_a = db.read_object("obj/a")
        # 原断言：assert db.read_object("obj/a") == "data_a"
        record("read_obj_a", read_a)
        read_b = db.read_object("obj/b")
        # 原断言：assert db.read_object("obj/b") == "data_b"
        record("read_obj_b", read_b)

        # _DB_META JSON 直读 + Database.load_meta 兼容层双验证。
        import json
        with open(meta_path, encoding="utf-8") as f:
            raw = json.load(f)
        created_at_positive = raw["created_at"] > 0
        # 原断言：assert raw["created_at"] > 0
        record("raw_created_at_positive", created_at_positive, detail=raw["created_at"])
        # 原断言：assert raw["version"] == 2
        record("raw_version", raw["version"])
        # 原断言：assert raw["uid"] is not None
        record("raw_uid_present", raw["uid"] is not None, detail=raw["uid"])

        meta = db.load_meta()
        # 保留：判空守卫——后续 meta.created_at 还要用 meta
        assert meta is not None
        meta_created_at_positive = meta.created_at > 0
        # 原断言：assert meta.created_at > 0
        record("meta_created_at_positive", meta_created_at_positive, detail=meta.created_at)
        # master 自写在非 stream 模式才登记 worker；本 case 只验证格式契约，
        # workers 可为空或含 master 登记，不做数量断言。

        db.freeze()
        db_frozen = db.is_frozen()
        # 原断言：assert db.is_frozen()
        record("db_frozen", db_frozen, detail=db_frozen)
        frozen_marker_exists = os.path.isfile(os.path.join(db_path, "_FROZEN"))
        # 原断言：assert os.path.isfile(os.path.join(db_path, "_FROZEN"))
        record("frozen_marker_exists", frozen_marker_exists, detail=frozen_marker_exists)

        storage.ex_stg_get_storage_manager().close_all()
        print("[DONE] test_db_meta_json_format", file=sys.stderr)

    finally:
        log.shutdown_log()
        for p in [db_path, log_dir]:
            cleanup(p)


test_db_meta_json_format()
print("\nAll load_db Phase 1 (standalone) tests passed!")
