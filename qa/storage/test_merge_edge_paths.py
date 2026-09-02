"""merge_db 校验分支 + 空库合并 + 跨 path merge 的链邻居边改写。

覆盖（2026-09 覆盖率批次 14 项之 3）：
  - merge_db 不存在路径 / 未 freeze / 无 _DB_META → 各自 RuntimeError
  - 空库（无 idx 文件）freeze 后 merge 成功（trivial empty merge）
  - A→B→C 链：merge B→B2 后 A.next[B].db_path 与 C.prev[B].db_path 同步改写，
    B 源目录删除，产物继承 uid + absorbed_from 记录
"""
import os
import shutil

from _fly_log import INFO

from fly import open_db, merge_db
from fly.runtime import get_agent
from storage import DbMetaFile

CASE_DIR = os.environ["FLY_CASE_LOG_DIR"]
BASE = os.path.join(CASE_DIR, "merge_edge")
PATH_A = os.path.join(BASE, "a")
PATH_B = os.path.join(BASE, "b")
PATH_B2 = os.path.join(BASE, "b2")
PATH_C = os.path.join(BASE, "c")
PATH_EMPTY = os.path.join(BASE, "empty_db")

if os.path.isdir(BASE):
    shutil.rmtree(BASE, ignore_errors=True)
os.makedirs(BASE, exist_ok=True)

master = get_agent()
master.launch_local_workers([{}])
from test import wait_until
assert wait_until(lambda: master.worker_count >= 1, timeout=30), "worker must connect"

# ── 1. 校验分支（顺序：isdir → _FROZEN → _DB_META）──────────────────
try:
    merge_db(os.path.join(BASE, "no_such_dir"))
    raise AssertionError("merge_db on missing path must raise")
except RuntimeError as e:
    assert "does not exist" in str(e), str(e)
INFO("[PASS] merge_db reject: path does not exist")

db_a = open_db(PATH_A)
try:
    merge_db(PATH_A)
    raise AssertionError("merge_db on unfrozen db must raise")
except RuntimeError as e:
    assert "not frozen" in str(e), str(e)
INFO("[PASS] merge_db reject: source not frozen")

frozen_no_meta = os.path.join(BASE, "frozen_no_meta")
os.makedirs(frozen_no_meta)
open(os.path.join(frozen_no_meta, "_FROZEN"), "w").close()
try:
    merge_db(frozen_no_meta)
    raise AssertionError("merge_db without _DB_META must raise")
except RuntimeError as e:
    assert "no _DB_META" in str(e), str(e)
INFO("[PASS] merge_db reject: no _DB_META")

# ── 2. 空库 merge 成功（无 idx 文件 → trivial empty merge）──────────
empty_db = open_db(PATH_EMPTY)
empty_db.freeze()
assert empty_db.is_frozen()
merged_empty = merge_db(PATH_EMPTY)
assert merged_empty.get_uid() == empty_db.get_uid()
INFO("[PASS] empty db merges trivially (no idx files)")

# ── 3. A→B→C 链，跨 path merge B → B2 ──────────────────────────────
db_c = open_db(PATH_C, prev=[open_db(PATH_B, prev=[db_a])])
b_uid = db_a.nexts()[0].get_uid()
assert b_uid is not None
INFO(f"[PASS] chain built A→B→C (b_uid={b_uid})")

db_b = db_a.nexts()[0]
db_b.freeze()
merged_b = merge_db(PATH_B, merge_db_path=PATH_B2, delete_source=True)

# 源目录彻底删除
assert not os.path.isdir(PATH_B), "merged source dir must be deleted"

# 产物继承身份 + absorbed_from
merged_chain = DbMetaFile(PATH_B2).read()
assert merged_chain["uid"] == b_uid, "target must inherit source uid"
assert PATH_B in merged_chain.get("absorbed_from", []), merged_chain

# 上游 A 的 next[B].db_path → B2
a_chain = DbMetaFile(PATH_A).read()
next_b = [e for e in a_chain["next"] if e["uid"] == b_uid]
assert len(next_b) == 1, a_chain
assert next_b[0]["db_path"] == PATH_B2, \
    f"upstream next edge must be rewritten to {PATH_B2}, got {next_b[0]}"

# 下游 C 的 prev[B].db_path → B2
c_chain = DbMetaFile(PATH_C).read()
prev_b = [e for e in c_chain["prev"] if e["uid"] == b_uid]
assert len(prev_b) == 1, c_chain
assert prev_b[0]["db_path"] == PATH_B2, \
    f"downstream prev edge must be rewritten to {PATH_B2}, got {prev_b[0]}"

# 返回句柄视角一致
assert merged_b.get_db_path() == PATH_B2
assert merged_b.get_uid() == b_uid
INFO("[PASS] cross-path merge rewrites both neighbor edges + deletes source")

master.stop()
INFO("[PASS] test_merge_edge_paths")
