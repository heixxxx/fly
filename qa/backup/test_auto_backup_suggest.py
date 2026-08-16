"""auto_backup 双层机制 e2e：worker TIER2 读流量 → suggest → master EWMA 判定 → 自动 backup。

2026-08-16 补覆盖：auto_backup 双层（worker suggest + master EWMA 聚合，architecture.md §5.4）
此前 QA 零覆盖（现有 case 全停留在 write_object(backup=True) 手动路径）。

链路：writer worker 持有对象（写不走 backup）→ reader worker 的 task 跨 worker TIER2
反复读（read_count 累积 ≥ worker_suggest_count_threshold）→ worker 上报
WorkerBackupSuggest → master EWMA 聚合（backup_count_threshold 低阈值）→
evaluate_and_maybe_backup 判定热点 → trigger_auto_backup。

断言：master.log 出现 "Auto-backup triggered"（判定链全通的铁证，INFO 级）。
capability 隔离（writer/reader）保证读必走 TIER2 而非数据持有者本地命中。
"""
from _fly_log import INFO
import os
import time
import shutil

from fly import as_task, open_db, get_config
from fly.runtime import get_agent

DB_PATH = os.path.join(get_config().get_str("log_dir"), "db")

if os.path.isdir(DB_PATH):
    shutil.rmtree(DB_PATH, ignore_errors=True)

# ── 双层判定低阈值（真实链路，只放大灵敏度）──
cfg = get_config()
cfg.set_int("auto_backup_enabled", 1)
cfg.set_int("worker_suggest_count_threshold", 3)   # reader 3 次跨 worker 读即上报
cfg.set_int("worker_suggest_cooldown", 0)
cfg.set_int("backup_count_threshold", 1)           # master 一次 suggest 即判热点
cfg.set_int("backup_bytes_threshold", 0)
cfg.set_int("max_backup_replicas", 2)
# 压小 ObjectCache low tier（64KB）+ 写 200KB 对象：对象超 cache 容量必不驻留，
# 每次读都走 TIER2 跨 worker 读——suggest 的 read_count 只在 TIER2 命中时累积
# （pickle 对象的 cache="none" 仍走 C++ low tier，无法绕过，故用容量隔离）。
cfg.set_int("read_cache_size", 65536)
HOT_VALUE = "x" * 200000

master = get_agent()
master.launch_local_workers([
    {"attributes": ["writer"]},   # obj 持有者
    {"attributes": ["reader"]},   # 跨 worker 读方（TIER2 流量来源）
])
assert master.wait_for_workers(2), "both workers should connect"


@as_task(requires=["writer"])
def write_obj(db, key, value):
    db.write_object(key, value)


@as_task(requires=["reader"])
def read_obj_many(db, key, times):
    from _fly_log import INFO as _INFO
    from fly import get_config as _gc
    _INFO(f"[reader-task] auto_backup_enabled={_gc().get_int('auto_backup_enabled')} "
          f"suggest_thr={_gc().get_int('worker_suggest_count_threshold')} "
          f"backup_thr={_gc().get_int('backup_count_threshold')}")
    for _ in range(times):
        # cache="none"：绕过 ObjectCache 强制每次走 TIER2 跨 worker 读——suggest 的
        # read_count 只在 TIER2 命中时累积（默认 "low" 会被本地缓存拦截到仅 1 次）。
        v = db.read_object(key)
        assert v == HOT_VALUE, "read mismatch"


db = open_db(DB_PATH)
write_obj(db, "hot/data", HOT_VALUE)
completed = master.wait_for_all_tasks(expected=1, timeout=30)
assert len(completed) >= 1, "write task should complete"

# reader task 读 6 次（≥ suggest 阈值 3）→ worker 累积并上报
read_obj_many(db, "hot/data", 6)
completed = master.wait_for_all_tasks(expected=2, timeout=30)
assert len(completed) >= 2, "read task should complete"

# 等 master 判定 + trigger（日志为 INFO 级；Logger 自动 flush ≤1s，运行中轮询可见）
master_log = os.path.join(get_config().get_str("log_dir"), "master.log")
assert os.path.exists(master_log), f"master.log should exist at {master_log}"

triggered = False
for _ in range(40):
    with open(master_log, errors="replace") as f:
        if "Auto-backup triggered: object=" in f.read():
            triggered = True
            break
    time.sleep(0.5)

assert triggered, "auto-backup should trigger from worker suggest + master EWMA"

# 等 internal backup task 完成（replicas 达到 2），保证 stop 时无悬挂 internal task
master.wait_for_all_tasks(expected=None, timeout=10)
INFO("[PASS] test_auto_backup_suggest: suggest → EWMA → auto-backup verified")

master.stop()
