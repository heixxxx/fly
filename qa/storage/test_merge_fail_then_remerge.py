"""E2E test: merge_db 部分失败 → 失败清理 → 重新 merge 成功。

验证用户领域约束（失败路径数据一致性）：
  1. merge 失败时 raise RuntimeError（不再静默返回句柄/发成功消息）；
  2. 源数据保留（未失败对象仍可读——支撑重新 merge）；
  3. 失败产物被清理（merge target 侧不残留半成品 .dat）；
  4. 恢复后重新 merge_db 成功，全部对象可读。

失败注入（确定性，无时序竞争）：write_data_no_cache（保存等级 "none"，
不进 low 缓存）写入 + aggregation_threshold 调小使每对象独占 data 文件，
merge 前删除其中一个 .dat —— 该对象 merge task 拉源失败（部分成功场景），
其余对象正常 merge 成功。
"""
from _fly_log import INFO
import os
import shutil
import threading
import time

from test import write_data_no_cache, record
from fly import open_db, merge_db, get_config
DB_PATH = os.path.join(get_config().get_str("log_dir"), "merge_fail_db")
MERGED_DATA = DB_PATH + ".merged_data"
from fly.runtime import get_agent


def cleanup():
    for p in (DB_PATH, MERGED_DATA):
        if os.path.isdir(p):
            shutil.rmtree(p, ignore_errors=True)


def wait_for(condition, timeout=30.0, interval=0.5):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if condition():
            return True
        time.sleep(interval)
    return False


def dat_files(path):
    """非空 .dat 文件列表（0 字节的是未写任何对象的空 writer 残留，排除）。"""
    if not os.path.isdir(path):
        return []
    return sorted(f for f in os.listdir(path)
                  if f.startswith("data_") and f.endswith(".dat")
                  and os.path.getsize(os.path.join(path, f)) > 0)


cleanup()
get_config().set_int("fail_unscheduleable_tasks", 0)
get_config().set_int("aggregation_threshold", 1)  # 每对象独占 data 文件（首个文件 0B 不滚动，后续写入前必滚动）

master = get_agent()
master.launch_local_workers([{"host": "host_A"}])
# 保留：等待原语
assert master.wait_workers_registered(timeout=60), "host_A worker should register"

db = open_db(DB_PATH)
# cache="none" 写入：不进 low 缓存（否则删 .dat 后仍可从缓存 serve，注入失效）。
write_data_no_cache(db, "mf/x", 111)
write_data_no_cache(db, "mf/z", 222)
# 保留：等待原语
assert wait_for(lambda: len(master.completed_tasks) >= 2), "2 writes should complete"
time.sleep(0.5)  # 等落盘

db.freeze()
db_frozen = db.is_frozen()
# 原断言：assert db.is_frozen(), "db should be frozen"
record("db_frozen", db_frozen, detail=db_frozen)

src_dats = dat_files(DB_PATH)
# 原断言：assert len(src_dats) == 2, f"each object should own one data file, got {src_dats}"
record("source_dat_count", len(src_dats), detail=src_dats)
# 保留：守卫——后面取 src_dats[0]，空列表时会以 IndexError 崩溃
assert len(src_dats) >= 1, f"each object should own one data file, got {src_dats}"

# ── 失败注入：删除一个 .dat（该对象拉源必然失败，其余对象正常成功）──
victim_file = os.path.join(DB_PATH, src_dats[0])
shutil.move(victim_file, victim_file + ".bak")  # 备份供恢复
INFO(f"[MERGE-FAIL] injected: removed {src_dats[0]}")

# ── merge 必须以 RuntimeError 失败（不再静默返回句柄）──
merge_error = None

def run_merge():
    global merge_error
    try:
        merge_db(DB_PATH, delete_source=True)
    except RuntimeError as e:
        merge_error = str(e)

t = threading.Thread(target=run_merge)
t.start()
t.join(timeout=120)
merge_raised = merge_error is not None
# 原断言：assert merge_error is not None, "merge_db with a broken object must raise RuntimeError"
record("merge_failed/error_type", "RuntimeError" if merge_error else None)
record("merge_failed_raises", merge_raised, detail=merge_error)
msg_mentions_failed = "failed" in merge_error.lower() if merge_error else False
# 原断言：assert "failed" in merge_error.lower(), f"error should describe failure: {merge_error}"
record("merge_failed_msg_mentions_failed", msg_mentions_failed, detail=merge_error)
INFO(f"[MERGE-FAIL] merge_db raised as expected: {merge_error}")

# ── 失败后：源数据保留 + 失败产物被清理 ──
time.sleep(1.0)  # 给 purge 广播（best-effort）一点时间

# 只读未删对象（z）：被删 .dat 的对象（x）不能读——不仅读不到（预期），且
# master 侧读失败会触发 TIER2 自愈把唯一 replica 位置从 remote_idx 踢掉
#（data_service TIER2 失败路径），导致恢复文件后重 merge 也找不到源。
# 该单副本脆弱性是现有自愈机制的固有行为，另行立项。
preserved_z = db.read_object("mf/z")
# 原断言：assert db.read_object("mf/z") == 222, \
record("preserved_z", preserved_z)
INFO("[MERGE-FAIL] source data preserved (readable)")

leftover = dat_files(MERGED_DATA)
no_leftover = not leftover
# 原断言：assert not leftover, f"failed merge products should be purged, found {leftover}"
record("failed_merge_products_purged", no_leftover, detail=leftover)
INFO("[MERGE-FAIL] no leftover products in merged data dir")

# ── 恢复注入的文件，重新 merge 成功 ──
shutil.move(victim_file + ".bak", victim_file)
merged_db = merge_db(DB_PATH, delete_source=True)
remerged_x = merged_db.read_object("mf/x")
# 原断言：assert merged_db.read_object("mf/x") == 111, "re-merge should read mf/x"
record("remerged_x", remerged_x)
remerged_z = merged_db.read_object("mf/z")
# 原断言：assert merged_db.read_object("mf/z") == 222, "re-merge should read mf/z"
record("remerged_z", remerged_z)
INFO("[MERGE-FAIL] re-merge succeeded, all objects readable")

# 成功路径语义：源 .dat 已删（delete_source=True），产物在 merged data。
src_dats_final = dat_files(DB_PATH)
# 原断言：assert not dat_files(DB_PATH), f"source should be deleted after successful merge: {dat_files(DB_PATH)}"
record("source_deleted_after_remerge", not src_dats_final, detail=len(src_dats_final))
merged_dats_final = dat_files(MERGED_DATA)
# 原断言：assert dat_files(MERGED_DATA), "merged data should have .dat files"
record("merged_data_nonempty", len(merged_dats_final) > 0,
       detail=len(merged_dats_final))

INFO("[DONE] test_merge_fail_then_remerge")
