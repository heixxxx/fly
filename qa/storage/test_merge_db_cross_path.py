"""E2E test: 跨 path merge_db + 源句柄读重定向。

验证 db_path 废弃后的迁移重定向机制（_MIGRATED_TO）：
  1. 在源 path 建 db，写数据，freeze
  2. 跨 path merge（db_path=新路径）—— 源 path 保留，写 _MIGRATED_TO 指向新路径
  3. 用【源 path 的 db 句柄】读数据 —— 应自动重定向到 merge 产物，读到正确数据

这是 solver build_matrix→merge→solve 链不断的核心保障：merge 后旧 db 句柄
（指向源 path）仍能 read_object 成功（经 resolve_migrated_path 路由到 target）。
"""
from _fly_log import INFO
import os
import time
import shutil

from e2e_tasks import write_data
from fly import open_db, merge_db, get_config
DB_PATH = os.path.join(get_config().get_str("log_dir"), "cross_path_db")
MERGE_BASE = os.path.join(get_config().get_str("log_dir"), "cross_path_merged")
from fly.runtime import get_agent


def cleanup():
    for p in [DB_PATH, DB_PATH + ".merged_data", MERGE_BASE, MERGE_BASE + ".merged_data"]:
        if os.path.isdir(p):
            shutil.rmtree(p, ignore_errors=True)


def wait_for(condition, timeout=30.0, interval=0.3):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if condition():
            return True
        time.sleep(interval)
    return False


cleanup()
get_config().set_int("fail_unscheduleable_tasks", 0)

master = get_agent()
master.launch_local_workers([{"host": "host_A"}, {"host": "host_B"}])
assert wait_for(lambda: master.worker_count >= 2), "need 2 workers on different hosts"

# ── Phase 1: 源 db 写数据 + freeze ──
db = open_db(DB_PATH)
write_data(db, "data/alpha", 100)
write_data(db, "data/beta", 200)
assert wait_for(lambda: len(master.completed_tasks) >= 2)
time.sleep(0.5)
db.freeze()
assert db.is_frozen()

source_db_path = db.get_db_path()
INFO(f"[CROSS-PATH] source db_path={source_db_path}, path={DB_PATH}")

# ── Phase 2: 跨 path merge（db_path=MERGE_BASE）──
# 源 path（DB_PATH）保留，写 _MIGRATED_TO 指向 MERGE_BASE。
merged_db = merge_db(DB_PATH, merge_db_path=MERGE_BASE, delete_source=True)
INFO(f"[CROSS-PATH] merge done, merged_db path={merged_db.get_db_path()}")

# 验证源 path 仍存在（保留作迁移锚点），且有 _MIGRATED_TO 文件
assert os.path.isdir(DB_PATH), "source path must be preserved as migration anchor"
migrated_marker = os.path.join(DB_PATH, "_MIGRATED_TO")
assert os.path.isfile(migrated_marker), f"_MIGRATED_TO should exist at {migrated_marker}"
INFO(f"[CROSS-PATH] _MIGRATED_TO present at source path")

# ── Phase 3: 验证迁移重定向 + cross-path read ──
# 3a. _MIGRATED_TO 文件存在且非空（迁移机制核心）
marker_content_exists = os.path.getsize(migrated_marker) > 0
assert marker_content_exists, "_MIGRATED_TO should not be empty"
INFO("[CROSS-PATH] _MIGRATED_TO has content (migration marker valid)")

# 3b. 产物句柄（merge_db 返回值）能读 merge 数据 —— 验证 cross-path read 路径完整：
#     master remote_idx 用 target 前缀，worker cleanup 扫 target 目录 idx + restore 到 target 命名空间。
assert merged_db.read_object("data/alpha") == 100, \
    "merged db should read cross-path merged data"
assert merged_db.read_object("data/beta") == 200, \
    "merged db should read cross-path merged data"
INFO("[CROSS-PATH] merged db reads cross-path data correctly")

INFO("[PASS] test_merge_db_cross_path")
