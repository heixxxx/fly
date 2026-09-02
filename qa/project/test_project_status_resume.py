"""Project status/resume/get_db 边界 + migrate 四类校验 + consolidate 未冻结校验。

覆盖（2026-09 覆盖率批次 14 项之 1，master 进程内）：
  - status() 返回 [{actual, logical, db_path, frozen}]，frozen 实时查询
  - load_project(path, resume=True)：无 failed_tasks.bin → no-op 正常返回
  - get_db(name, latest=True) 无候选 → KeyError
  - migrate 四类校验：new_path 相同 / 已存在 / db 目录缺失或在外 / data_path 不自包含
  - migrate_project(consolidate=True) 遇未冻结 db → RuntimeError
"""
import os
import shutil

from _fly_log import INFO

from fly import open_project, migrate_project
from fly.runtime import get_agent

CASE_DIR = os.environ["FLY_CASE_LOG_DIR"]
PROJ_PATH = os.path.join(CASE_DIR, "proj_status")
PROJ_MIGRATED = os.path.join(CASE_DIR, "proj_migrated")

if os.path.isdir(PROJ_PATH):
    shutil.rmtree(PROJ_PATH, ignore_errors=True)
if os.path.isdir(PROJ_MIGRATED):
    shutil.rmtree(PROJ_MIGRATED, ignore_errors=True)

master = get_agent()
master.launch_local_workers([{}])
from test import wait_until
assert wait_until(lambda: master.worker_count >= 1, timeout=30), "worker must connect"

# ── 1. 建 project（frozen + unfrozen 各一 db）─────────────────────────
proj = open_project(PROJ_PATH)
frozen_db = proj._create_db("frozen_one")
unfrozen_db = proj._create_db("unfrozen_one")

from fly import as_task


@as_task()
def write_val(db):
    db.write_object("val", 7)


write_val(frozen_db)
import time
t0 = time.time()
while len(master.completed_tasks) < 1:
    assert time.time() - t0 < 60, f"write task must finish: failed={master.failed_tasks}"
    time.sleep(0.1)
frozen_db.freeze()
assert wait_until(lambda: proj.is_db_frozen("frozen_one"), timeout=30), \
    "frozen_one should be frozen"
INFO("[PASS] setup: project with 1 frozen + 1 unfrozen db")

# ── 2. status() 返回结构 + frozen 实时 ───────────────────────────────
snap = proj.status()
assert isinstance(snap, list) and len(snap) == 2
by_actual = {s["actual"]: s for s in snap}
assert by_actual["frozen_one"]["frozen"] is True, snap
assert by_actual["unfrozen_one"]["frozen"] is False, snap
for s in snap:
    assert set(s.keys()) == {"actual", "logical", "db_path", "frozen"}, s
    assert s["logical"] in ("frozen_one", "unfrozen_one")
    assert os.path.isdir(s["db_path"])
INFO("[PASS] status() structure + realtime frozen")

# ── 3. load_project(resume=True) 无 bin → no-op ─────────────────────
reloaded = None
from fly import load_project
reloaded = load_project(PROJ_PATH, resume=True)
assert reloaded is not None
assert sorted(reloaded.list_dbs()) == ["frozen_one", "unfrozen_one"], \
    reloaded.list_dbs()
INFO("[PASS] load_project(resume=True) without failed_tasks.bin is a no-op")

# ── 4. get_db(latest=True) 无候选 → KeyError ────────────────────────
try:
    proj.get_db("no_such_logical", latest=True)
    raise AssertionError("get_db(latest=True) with no candidate must raise KeyError")
except KeyError as e:
    assert "no_such_logical" in str(e)
# 精确匹配分支同样 KeyError
try:
    proj.get_db("no_such_actual")
    raise AssertionError("get_db with unknown actual must raise KeyError")
except KeyError:
    pass
INFO("[PASS] get_db unknown name raises KeyError (exact + latest)")

# ── 5. migrate 四类校验 ─────────────────────────────────────────────
# 5a. new_path 与当前相同
try:
    proj.migrate(proj.db_path)
    raise AssertionError("migrate to identical path must raise")
except RuntimeError as e:
    assert "identical" in str(e), str(e)
INFO("[PASS] migrate reject: identical new_path")

# 5b. new_path 已存在
os.makedirs(PROJ_MIGRATED, exist_ok=True)
try:
    proj.migrate(PROJ_MIGRATED)
    raise AssertionError("migrate to existing path must raise")
except RuntimeError as e:
    assert "already exists" in str(e), str(e)
shutil.rmtree(PROJ_MIGRATED, ignore_errors=True)
INFO("[PASS] migrate reject: existing new_path")

# 5c-i. db 目录缺失
missing_meta = dict(proj._meta)
info = proj._meta["dbs"]["unfrozen_one"]
real_bp = info["db_path"]
info["db_path"] = os.path.join(CASE_DIR, "ghost_db_dir")
try:
    proj.migrate(PROJ_MIGRATED)
    raise AssertionError("migrate with missing db dir must raise")
except RuntimeError as e:
    assert "db dir missing" in str(e), str(e)
finally:
    info["db_path"] = real_bp
INFO("[PASS] migrate reject: missing db dir")

# 5c-ii. db 在 project 目录外（meta 快照被改写的场景）
outside_dir = os.path.join(CASE_DIR, "outside_holder")
os.makedirs(outside_dir, exist_ok=True)
info["db_path"] = outside_dir
try:
    proj.migrate(PROJ_MIGRATED)
    raise AssertionError("migrate with db outside project must raise")
except RuntimeError as e:
    assert "outside project dir" in str(e), str(e)
finally:
    info["db_path"] = real_bp
shutil.rmtree(outside_dir, ignore_errors=True)
INFO("[PASS] migrate reject: db lives outside project")

# 5d. data_path 不自包含（指向 project 外）
real_dp = info.get("data_path", "")
info["data_path"] = CASE_DIR  # project 外的既有目录
try:
    proj.migrate(PROJ_MIGRATED)
    raise AssertionError("migrate with data_path outside project must raise")
except RuntimeError as e:
    assert "not self-contained" in str(e), str(e)
finally:
    info["data_path"] = real_dp
INFO("[PASS] migrate reject: data_path outside project")

# ── 6. consolidate=True 遇未冻结 db → RuntimeError ──────────────────
try:
    migrate_project(PROJ_PATH, consolidate=True)
    raise AssertionError("consolidate with unfrozen db must raise")
except RuntimeError as e:
    assert "not" in str(e) and "frozen" in str(e), str(e)
    assert "unfrozen_one" in str(e), f"should list the unfrozen db: {e}"
INFO("[PASS] migrate_project(consolidate=True) rejects unfrozen db")

master.stop()
INFO("[PASS] test_project_status_resume")
