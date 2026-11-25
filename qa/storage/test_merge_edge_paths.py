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

from test import record
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
# 保留：等待原语
assert wait_until(lambda: master.worker_count >= 1, timeout=30), "worker must connect"

# ── 1. 校验分支（顺序：isdir → _FROZEN → _DB_META）──────────────────
raised = None
msg_contains = False
try:
    merge_db(os.path.join(BASE, "no_such_dir"))
    raise AssertionError("merge_db on missing path must raise")
except RuntimeError as e:
    raised = {"type": type(e).__name__, "msg": str(e)}
    msg_contains = "does not exist" in raised["msg"]
# 原断言：assert "does not exist" in str(e), str(e)
record("reject_missing_path/error_type", raised["type"] if raised else None)
record("reject_missing_path/msg_mentions_does_not_exist", msg_contains, detail=raised)
INFO("[DONE] merge_db reject: path does not exist")

db_a = open_db(PATH_A)
raised = None
msg_contains = False
try:
    merge_db(PATH_A)
    raise AssertionError("merge_db on unfrozen db must raise")
except RuntimeError as e:
    raised = {"type": type(e).__name__, "msg": str(e)}
    msg_contains = "not frozen" in raised["msg"]
# 原断言：assert "not frozen" in str(e), str(e)
record("reject_not_frozen/error_type", raised["type"] if raised else None)
record("reject_not_frozen/msg_mentions_not_frozen", msg_contains, detail=raised)
INFO("[DONE] merge_db reject: source not frozen")

frozen_no_meta = os.path.join(BASE, "frozen_no_meta")
os.makedirs(frozen_no_meta)
open(os.path.join(frozen_no_meta, "_FROZEN"), "w").close()
raised = None
msg_contains = False
try:
    merge_db(frozen_no_meta)
    raise AssertionError("merge_db without _DB_META must raise")
except RuntimeError as e:
    raised = {"type": type(e).__name__, "msg": str(e)}
    msg_contains = "no _DB_META" in raised["msg"]
# 原断言：assert "no _DB_META" in str(e), str(e)
record("reject_no_meta/error_type", raised["type"] if raised else None)
record("reject_no_meta/msg_mentions_no_db_meta", msg_contains, detail=raised)
INFO("[DONE] merge_db reject: no _DB_META")

# ── 2. 空库 merge 成功（无 idx 文件 → trivial empty merge）──────────
empty_db = open_db(PATH_EMPTY)
empty_db.freeze()
empty_frozen = empty_db.is_frozen()
# 原断言：assert empty_db.is_frozen()
record("empty_merge/frozen", empty_frozen, detail=empty_frozen)
merged_empty = merge_db(PATH_EMPTY)
empty_db_uid = empty_db.get_uid()
merged_empty_uid = merged_empty.get_uid()
uid_inherited = merged_empty_uid == empty_db_uid
# 原断言：assert merged_empty.get_uid() == empty_db.get_uid()
record("empty_merge/uid_inherited", uid_inherited,
       detail={"merged": merged_empty_uid, "source": empty_db_uid})
INFO("[DONE] empty db merges trivially (no idx files)")

# ── 3. A→B→C 链，跨 path merge B → B2 ──────────────────────────────
db_c = open_db(PATH_C, prev=[open_db(PATH_B, prev=[db_a])])
b_uid = db_a.nexts()[0].get_uid()
# 原断言：assert b_uid is not None
record("chain/b_uid_present", b_uid is not None, detail=b_uid)
INFO(f"[DONE] chain built A→B→C (b_uid={b_uid})")

db_b = db_a.nexts()[0]
db_b.freeze()
merged_b = merge_db(PATH_B, merge_db_path=PATH_B2, delete_source=True)

# 源目录彻底删除
source_deleted = not os.path.isdir(PATH_B)
# 原断言：assert not os.path.isdir(PATH_B), "merged source dir must be deleted"
record("merge_b/source_deleted", source_deleted, detail=source_deleted)

# 产物继承身份 + absorbed_from
merged_chain = DbMetaFile(PATH_B2).read()
merged_uid_match = merged_chain["uid"] == b_uid
# 原断言：assert merged_chain["uid"] == b_uid, "target must inherit source uid"
record("merge_b/uid_inherited", merged_uid_match, detail=merged_chain)
absorbed = merged_chain.get("absorbed_from", [])
absorbed_contains = PATH_B in absorbed
# 原断言：assert PATH_B in merged_chain.get("absorbed_from", []), merged_chain
record("merge_b/absorbed_from_contains_source", absorbed_contains, detail=absorbed)

# 上游 A 的 next[B].db_path → B2
a_chain = DbMetaFile(PATH_A).read()
next_b = [e for e in a_chain["next"] if e["uid"] == b_uid]
# 原断言：assert len(next_b) == 1, a_chain
record("edge_a_next_b/edge_count", len(next_b), detail=a_chain)
next_b_path = next_b[0]["db_path"] if next_b else None
# 原断言：assert next_b[0]["db_path"] == PATH_B2, \
record("edge_a_next_b/db_path", next_b_path)

# 下游 C 的 prev[B].db_path → B2
c_chain = DbMetaFile(PATH_C).read()
prev_b = [e for e in c_chain["prev"] if e["uid"] == b_uid]
# 原断言：assert len(prev_b) == 1, c_chain
record("edge_c_prev_b/edge_count", len(prev_b), detail=c_chain)
prev_b_path = prev_b[0]["db_path"] if prev_b else None
# 原断言：assert prev_b[0]["db_path"] == PATH_B2, \
record("edge_c_prev_b/db_path", prev_b_path)

# 返回句柄视角一致
merged_handle_path = merged_b.get_db_path()
# 原断言：assert merged_b.get_db_path() == PATH_B2
record("merged_handle/db_path", merged_handle_path)
merged_handle_uid_match = merged_b.get_uid() == b_uid
# 原断言：assert merged_b.get_uid() == b_uid
record("merged_handle/uid_matches", merged_handle_uid_match,
       detail={"merged": merged_b.get_uid(), "expected": b_uid})
INFO("[DONE] cross-path merge rewrites both neighbor edges + deletes source")

master.stop()
INFO("[DONE] test_merge_edge_paths")
