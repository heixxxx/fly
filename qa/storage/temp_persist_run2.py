"""run2：load_db 恢复（含 temp idx）→ 跨进程读 temp 对象成功（task 级断点核心路径）。"""
import os

from _fly_log import INFO
from fly import load_db

DB_PATH = os.environ["FLY_DB_PATH"]

db = load_db(DB_PATH)

# 正式对象（对照）。
assert db.read_object("final/result") == {"ok": True}

# temp 对象：load_db 恢复 temp idx（worker restore + master rebuild mark_data_ready）
# → master 读走 TIER3 定位 + TIER2 worker 盘读 fallback。
v0 = db.read_object("iters/state_0")
assert v0["step"] == 0 and v0["arr"][:3] == [0, 1, 2], f"temp obj restored: {v0}"
v1 = db.read_object("iters/state_1")
assert v1["step"] == 1 and v1["arr"][0] == 100, f"temp obj restored: {v1}"

# 冻结后 temp 清理（同进程验证 freeze 语义收口）。
db.freeze()
import time as _t
_t0 = _t.time()
while not db.is_frozen() and _t.time() - _t0 < 30:
    _t.sleep(0.2)
assert db.is_frozen()
residue = [f for f in os.listdir(DB_PATH)
           if f.startswith("temp_data_") or f.endswith(".temp.idx")]
assert not residue, f"freeze 后 temp 文件必须清空，残留: {residue}"

INFO("[PASS] temp_persist_run2: temp restored across processes + cleaned on freeze")
