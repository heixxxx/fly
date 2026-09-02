"""agent wait/ensure 家族边界路径 + load_db 无 worker auto-start。

覆盖（2026-09 覆盖率批次 14 项之 5）：
  - ensure_workers 参数校验：空串属性 / 非字符串元素 / 空 list → ValueError
  - expect_workers 登记占位符 + wait_workers_registered(timeout=1) → False
  - wait_for_workers(count=99, timeout=0.5) → False
  - wait_for_all_tasks(expected=1) 遇必败 task → RuntimeError("Tasks failed")
  - kill 全部 worker 后 load_db：meta 有 hostname 记录 → auto-start spawn；
    配合 worker_register_timeout + 幽灵占位符 → 注册超时 TimeoutError 分支
（submit 未 start 自动 start 分支：qa 进程 master 天然已 start，人为翻转
  _running 会二次调 C++ start，行为未定义，不覆盖。）
"""
import os
import shutil
import signal
import time

from _fly_log import INFO

from fly import open_db, load_db, as_task, expect_workers, ensure_workers
from fly import wait_workers_registered, get_config
from fly.runtime import get_agent
from test import wait_until

CASE_DIR = os.environ["FLY_CASE_LOG_DIR"]
DB_PATH = os.path.join(CASE_DIR, "wait_paths_db")

if os.path.isdir(DB_PATH):
    shutil.rmtree(DB_PATH, ignore_errors=True)

master = get_agent()

# ── 1. ensure_workers 参数校验（提交前即抛，不触碰集群）──────────────
for bad in ([""], [["qa", 42]], []):
    try:
        ensure_workers(bad, timeout=0.5)
        raise AssertionError(f"ensure_workers({bad!r}) must raise ValueError")
    except ValueError:
        pass
INFO("[PASS] ensure_workers argument validation (empty str / non-str / empty list)")

# ── 2. 占位符登记 + 注册等待超时 → False ─────────────────────────────
expect_workers([998])
assert wait_workers_registered(timeout=1.0) is False, \
    "ghost placeholder must keep wait_workers_registered returning False"
INFO("[PASS] expect_workers + wait_workers_registered(timeout) -> False")

assert master.wait_for_workers(count=99, timeout=0.5) is False, \
    "impossible count must time out to False"
INFO("[PASS] wait_for_workers(count=99, timeout=0.5) -> False")

# ── 3. 必败 task → wait_for_all_tasks(expected=1) RuntimeError ──────
get_config().set_int("fail_unscheduleable_tasks", 0)
master.launch_local_workers([{}])
assert wait_until(lambda: master.worker_count >= 1, timeout=30), "worker must connect"

db = open_db(DB_PATH)


@as_task()
def failing(db):
    raise RuntimeError("intended failure for wait-path test")


failing(db)
try:
    master.wait_for_all_tasks(expected=1, timeout=30)
    raise AssertionError("wait_for_all_tasks must raise on failed task")
except RuntimeError as e:
    assert "Tasks failed" in str(e), str(e)
INFO("[PASS] wait_for_all_tasks(expected=1) raises on failed task")

# ── 4. load_db auto-start spawn（meta 有无 worker 的 host → spawn）──
# 直接向 _DB_META.workers 注入一条假 host 记录：load_db 发现该 host 无在线
# worker → auto-start spawn --host ghost_host_xyz。
# （曾试过 SIGKILL 真 worker 触发：连接判死后 get_worker_hostnames 的注册表
#   不随宽限过期，Phase 2 视为该 host 仍有 worker，不 spawn——不可用。）
from storage import DbMetaFile


def _add_ghost_worker(d):
    d["workers"] = d.get("workers", []) + [{
        "worker_id": 42, "writer_id": "ghostw1", "hostname": "ghost_host_xyz",
        "ip_address": "127.0.0.1", "launch_command": ""}]
    return d


DbMetaFile(DB_PATH).update(_add_ghost_worker)

# 幽灵占位符 + 1s 注册超时：spawn 的 worker 正常注册，但 999 永不注册 →
# _wait_spawned_workers 走 cfg_timeout>0 分支 → TimeoutError。
get_config().set_int("worker_register_timeout", 1)
expect_workers([999])
try:
    load_db(DB_PATH)
    raise AssertionError("load_db with ghost placeholder must hit register timeout")
except TimeoutError as e:
    assert "failed to register" in str(e), str(e)
INFO("[PASS] load_db auto-start spawn + register-timeout branch")

master.stop()
INFO("[PASS] test_agent_wait_paths")
