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
from test import record
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
    # 保留：等待循环体内的断言——写任务没完成就退出循环会让后面的 freeze 检查失去前提
    assert time.time() - t0 < 60, f"write task must finish: failed={master.failed_tasks}"
    time.sleep(0.1)
frozen_db.freeze()
assert wait_until(lambda: proj.is_db_frozen("frozen_one"), timeout=30), \
    "frozen_one should be frozen"
INFO("[DONE] setup: project with 1 frozen + 1 unfrozen db")

# ── 2. status() 返回结构 + frozen 实时 ───────────────────────────────
snap = proj.status()
snap_len = len(snap)
# 原断言：assert isinstance(snap, list) and len(snap) == 2
record("status/type", type(snap).__name__)
record("status/db_count", snap_len)
by_actual = {s["actual"]: s for s in snap}
frozen_one_frozen = by_actual["frozen_one"]["frozen"] is True
# 原断言：assert by_actual["frozen_one"]["frozen"] is True, snap
record("status/frozen_one_frozen", frozen_one_frozen, detail=snap)
unfrozen_one_unfrozen = by_actual["unfrozen_one"]["frozen"] is False
# 原断言：assert by_actual["unfrozen_one"]["frozen"] is False, snap
record("status/unfrozen_one_not_frozen", unfrozen_one_unfrozen, detail=snap)
# 循环遍历的是运行时集合（snap），记录不能写在循环里：先累计，循环结束后记录。
fields_ok = True
fields_bad = []
for s in snap:
    s_ok = (set(s.keys()) == {"actual", "logical", "db_path", "frozen"}
            and s.get("logical") in ("frozen_one", "unfrozen_one")
            and os.path.isdir(s["db_path"]))
    if not s_ok:
        fields_bad.append(s)
    fields_ok = fields_ok and s_ok
# 原断言：assert set(s.keys()) == {"actual", "logical", "db_path", "frozen"}, s
# 原断言：assert s["logical"] in ("frozen_one", "unfrozen_one")
# 原断言：assert os.path.isdir(s["db_path"])
record("status/entry_fields", fields_ok, detail=fields_bad)
INFO("[DONE] status() structure + realtime frozen")

# ── 3. load_project(resume=True) 无 bin → no-op ─────────────────────
reloaded = None
from fly import load_project
reloaded = load_project(PROJ_PATH, resume=True)
# 原断言：assert reloaded is not None
record("status/reloaded_project", reloaded is not None,
       detail=type(reloaded).__name__ if reloaded is not None else None)
# 保留：守卫——后面的 list_dbs 调用依赖 reloaded 非空
assert reloaded is not None
# 原断言：assert sorted(reloaded.list_dbs()) == ["frozen_one", "unfrozen_one"], \
record("status/list_dbs", sorted(reloaded.list_dbs()))
INFO("[DONE] load_project(resume=True) without failed_tasks.bin is a no-op")

# ── 4. get_db(latest=True) 无候选 → KeyError ────────────────────────
latest_err = None
try:
    proj.get_db("no_such_logical", latest=True)
    raise AssertionError("get_db(latest=True) with no candidate must raise KeyError")  # 保留：否定性检查——没抛 KeyError 时直接失败
except KeyError as e:
    latest_err = {"type": type(e).__name__, "msg": str(e)}
# 原断言：assert "no_such_logical" in str(e)
record("status/get_db_latest_msg_has_name",
       latest_err is not None and "no_such_logical" in latest_err["msg"],
       detail=latest_err)
# 精确匹配分支同样 KeyError
try:
    proj.get_db("no_such_actual")
    raise AssertionError("get_db with unknown actual must raise KeyError")  # 保留：否定性检查——没抛 KeyError 时直接失败
except KeyError:
    pass
INFO("[DONE] get_db unknown name raises KeyError (exact + latest)")

# ── 5. migrate 四类校验 ─────────────────────────────────────────────
# 5a. new_path 与当前相同
identical_err = None
try:
    proj.migrate(proj.db_path)
    raise AssertionError("migrate to identical path must raise")  # 保留：否定性检查——没抛 RuntimeError 时直接失败
except RuntimeError as e:
    identical_err = str(e)
# 原断言：assert "identical" in str(e), str(e)
record("migrate_reject/identical_msg",
       identical_err is not None and "identical" in identical_err,
       detail=identical_err)
INFO("[DONE] migrate reject: identical new_path")

# 5b. new_path 已存在
os.makedirs(PROJ_MIGRATED, exist_ok=True)
existing_err = None
try:
    proj.migrate(PROJ_MIGRATED)
    raise AssertionError("migrate to existing path must raise")  # 保留：否定性检查——没抛 RuntimeError 时直接失败
except RuntimeError as e:
    existing_err = str(e)
# 原断言：assert "already exists" in str(e), str(e)
record("migrate_reject/existing_msg",
       existing_err is not None and "already exists" in existing_err,
       detail=existing_err)
shutil.rmtree(PROJ_MIGRATED, ignore_errors=True)
INFO("[DONE] migrate reject: existing new_path")

# 5c-i. db 目录缺失
missing_meta = dict(proj._meta)
info = proj._meta["dbs"]["unfrozen_one"]
real_bp = info["db_path"]
info["db_path"] = os.path.join(CASE_DIR, "ghost_db_dir")
missing_err = None
try:
    proj.migrate(PROJ_MIGRATED)
    raise AssertionError("migrate with missing db dir must raise")  # 保留：否定性检查——没抛 RuntimeError 时直接失败
except RuntimeError as e:
    missing_err = str(e)
finally:
    info["db_path"] = real_bp
# 原断言：assert "db dir missing" in str(e), str(e)
record("migrate_reject/missing_dir_msg",
       missing_err is not None and "db dir missing" in missing_err,
       detail=missing_err)
INFO("[DONE] migrate reject: missing db dir")

# 5c-ii. db 在 project 目录外（meta 快照被改写的场景）
outside_dir = os.path.join(CASE_DIR, "outside_holder")
os.makedirs(outside_dir, exist_ok=True)
info["db_path"] = outside_dir
outside_err = None
try:
    proj.migrate(PROJ_MIGRATED)
    raise AssertionError("migrate with db outside project must raise")  # 保留：否定性检查——没抛 RuntimeError 时直接失败
except RuntimeError as e:
    outside_err = str(e)
finally:
    info["db_path"] = real_bp
# 原断言：assert "outside project dir" in str(e), str(e)
record("migrate_reject/outside_msg",
       outside_err is not None and "outside project dir" in outside_err,
       detail=outside_err)
shutil.rmtree(outside_dir, ignore_errors=True)
INFO("[DONE] migrate reject: db lives outside project")

# 5d. data_path 不自包含（指向 project 外）
real_dp = info.get("data_path", "")
info["data_path"] = CASE_DIR  # project 外的既有目录
notself_err = None
try:
    proj.migrate(PROJ_MIGRATED)
    raise AssertionError("migrate with data_path outside project must raise")  # 保留：否定性检查——没抛 RuntimeError 时直接失败
except RuntimeError as e:
    notself_err = str(e)
finally:
    info["data_path"] = real_dp
# 原断言：assert "not self-contained" in str(e), str(e)
record("migrate_reject/not_selfcontained_msg",
       notself_err is not None and "not self-contained" in notself_err,
       detail=notself_err)
INFO("[DONE] migrate reject: data_path outside project")

# ── 6. consolidate=True 遇未冻结 db → RuntimeError ──────────────────
consol_err = None
try:
    migrate_project(PROJ_PATH, consolidate=True)
    raise AssertionError("consolidate with unfrozen db must raise")  # 保留：否定性检查——没抛 RuntimeError 时直接失败
except RuntimeError as e:
    consol_err = str(e)
# 原断言：assert "not" in str(e) and "frozen" in str(e), str(e)
# 原断言：assert "unfrozen_one" in str(e), f"should list the unfrozen db: {e}"
record("migrate_reject/consolidate_unfrozen_msg",
       consol_err is not None and "not" in consol_err and "frozen" in consol_err
       and "unfrozen_one" in consol_err,
       detail=consol_err)
INFO("[DONE] migrate_project(consolidate=True) rejects unfrozen db")

master.stop()
INFO("[DONE] test_project_status_resume")
