"""缓存等级语义 e2e（2026-08-30 双池裁定）。

验证：
  1. read_object 默认 populate low（第二次读命中——缓存池条目存在）
  2. temp 对象读后路由 temp 池（不占主池字节）
  3. write_object(cache=...) 写后预热
  4. read_object(cache="none") 显式零缓存（不 populate）
  5. 缓存失效：写后 invalidate（remove+rewrite 后读新值）
"""
from _fly_log import INFO
import os
import shutil

from test import write_data
from fly import open_db, get_config
DB_PATH = os.path.join(get_config().get_str("log_dir"), "db")


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


def wait_for(condition, timeout=60.0, interval=0.5):
    import time
    t0 = time.time()
    while time.time() - t0 < timeout:
        if condition():
            return True
        time.sleep(interval)
    return False


cleanup()
get_config().set_int("fail_unscheduleable_tasks", 0)

from fly.runtime import get_agent
master = get_agent()

master.launch_local_workers([{}])
assert master.wait_for_workers(1)

db = open_db(DB_PATH)
from storage import get_read_cache
rc = get_read_cache()

# ── 1. 正式对象：默认 read populate low ──
write_data(db, "lvl/obj", 42)
assert wait_for(lambda: len(master.completed_tasks) >= 1, timeout=30.0)

v = db.read_object("lvl/obj")  # 默认 cache="low"
assert v == 42
key = f"{DB_PATH}:lvl/obj"
entry = rc._main.get(key)
assert entry is not None, "默认读应 populate 主池"
assert entry.level == "low", f"默认等级应为 low，got {entry.level}"
v2 = db.read_object("lvl/obj")  # 第二次读：命中缓存
assert v2 == 42
INFO("[PASS] read_object 默认 populate low + 命中")

# ── 2. 显式 high 升级（重写后用 high 读 → 主池 high 等级）──
db.remove_object("lvl/obj")
db.write_object("lvl/obj", 43)
v = db.read_object("lvl/obj", cache="high")
assert v == 43
assert rc._main[key].level == "high", "显式 high 读应升级等级"
INFO("[PASS] 显式 high 读取升级等级")

# ── 3. temp 对象：读后路由 temp 池 ──
from test import write_temp
write_temp(db, "lvl/temp_obj", "temp_val")
assert wait_for(lambda: len(master.completed_tasks) >= 2, timeout=30.0)

tv = db.read_object("lvl/temp_obj")
assert tv == "temp_val"
tkey = f"{DB_PATH}:lvl/temp_obj"
assert tkey in rc._temp, "temp 对象读后应入 temp 池"
assert tkey not in rc._main, "temp 对象不得占主池"
assert rc._main.get(key) is not None, "temp 读不影响主池既有条目"
INFO("[PASS] temp 对象路由独立 temp 池")

# ── 4. write 预热（正式 low / temp 池）──
db.write_object("lvl/warm", "warm_val", cache="low")
assert rc._main[f"{DB_PATH}:lvl/warm"].level == "low", "写后预热应入主池 low"
db.write_object("lvl/warm_t", "wt", save_to_db=False, cache="low")
assert f"{DB_PATH}:lvl/warm_t" in rc._temp, "temp 写预热应入 temp 池"
INFO("[PASS] write 预热（正式→主池 / temp→temp 池）")

# ── 5. cache="none" 显式零缓存 ──
db.remove_object("lvl/none_obj")
db.write_object("lvl/none_obj", "x")
v = db.read_object("lvl/none_obj", cache="none")
assert v == "x"
assert f"{DB_PATH}:lvl/none_obj" not in rc._main, "none 不得 populate"
INFO("[PASS] cache=none 显式零缓存")

# ── 6. 失效一致性：remove+rewrite 后读到新值（不吐陈旧缓存）──
db.remove_object("lvl/obj")
db.write_object("lvl/obj", 44)
assert db.read_object("lvl/obj") == 44, "重写后必须读到新值（缓存失效）"
INFO("[PASS] 写后失效一致性")

INFO("[PASS] test_cache_level_semantics")
cleanup()
