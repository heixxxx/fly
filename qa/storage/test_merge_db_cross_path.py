"""E2E test: 跨 path merge_db + 源彻底删除（db chain 机制）。

验证 db chain 机制取代 _MIGRATED_TO 后的跨 path merge 行为：
  1. 在源 path 建 db，写数据，freeze
  2. 跨 path merge（db_path=新路径）—— **源 path 彻底删除**，不写 _MIGRATED_TO
  3. merge 产物句柄能读全部数据
  4. _DB_CHAIN 在 target 继承 source 身份（uid 不变）
  5. 源 path 不再存在（无遗留）
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
# db chain 机制：源 path 彻底删除，不写 _MIGRATED_TO。
merged_db = merge_db(DB_PATH, merge_db_path=MERGE_BASE, delete_source=True)
INFO(f"[CROSS-PATH] merge done, merged_db path={merged_db.get_db_path()}")

# 验证源 path 已彻底删除（无遗留，不再像旧机制保留作迁移锚点）
assert not os.path.isdir(DB_PATH), \
    f"source path should be deleted after merge (db chain mechanism), but {DB_PATH} still exists"
INFO(f"[CROSS-PATH] source path deleted (no _MIGRATED_TO residue)")

# ── Phase 3: 验证产物数据可读 + _DB_CHAIN 继承 ──
# 3a. 产物句柄（merge_db 返回值）能读 merge 数据
assert merged_db.read_object("data/alpha") == 100, \
    "merged db should read cross-path merged data"
assert merged_db.read_object("data/beta") == 200, \
    "merged db should read cross-path merged data"
INFO("[CROSS-PATH] merged db reads cross-path data correctly")

# 3b. 验证 _DB_CHAIN 在 target 继承 source 身份（uid 存在、有 absorbed_from）
target_chain_path = os.path.join(MERGE_BASE, "_DB_CHAIN")
assert os.path.isfile(target_chain_path), \
    f"_DB_CHAIN should exist at target {target_chain_path}"

try:
    from storage.py.db_chain import DbChainFile
except ImportError:
    from db_chain import DbChainFile

target_cf = DbChainFile(MERGE_BASE)
target_chain = target_cf.read()
assert target_chain is not None, "target _DB_CHAIN should be readable"
assert target_chain.get("uid") is not None, "target should inherit source uid"
assert DB_PATH in target_chain.get("absorbed_from", []), \
    f"target absorbed_from should contain source path {DB_PATH}"
INFO(f"[CROSS-PATH] target _DB_CHAIN: uid={target_chain['uid']}, "
     f"absorbed_from={target_chain.get('absorbed_from')}")

INFO("[PASS] test_merge_db_cross_path")
