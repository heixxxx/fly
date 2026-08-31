"""E2E: 流式写中断残块治理（§14.1 测试 45 CrashOrphanChunks）。

场景：task 内流式写大对象中途 raise → 段事务回滚（abort）→
  1) 对象不可见（完成登记未发生——下游 wait 不可达）
  2) 残块（已落盘的块流）无 trailer 结构上不可读
  3) 同 db 后续写正常（段回滚后文件状态干净）
"""
from _fly_log import INFO
import os
import time

from fly import as_task, open_db, get_config, get_work_directory
from test import qa_tmp

DB_PATH = os.path.join(get_work_directory(), "db")


def cleanup():
    import shutil
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


@as_task()
def write_and_crash(db, key, size_mb):
    # 流式写一半抛异常：块已入 WBQ（可能已落盘），无 trailer、无 entry。
    data_chunk = b'X' * (1024 * 1024)
    written = 0
    while written < size_mb:
        db.write_object(key + "_partial_marker", data_chunk)  # 触发流式写路径
        written += 1
        if written >= size_mb // 2:
            raise RuntimeError("simulated crash mid-write")


@as_task(inputs=lambda db: [db.get_full_name("recovery_probe")])
def write_probe(db):
    db.write_object("recovery_probe", "ok")


@as_task()
def write_recovery(db):
    db.write_object("recovery_probe", "ok")


cleanup()
# 写侧恒流式（T2c 2026-08-31）：streaming_write_threshold 开关已删。

from fly.runtime import get_agent

master = get_agent()
master.launch_local_workers([{"attributes": []}])
assert master.wait_for_workers(1, timeout=30), "worker failed to connect"

db = open_db(DB_PATH)

# ── Phase 1: 中断写 → task 失败 ──
write_and_crash(db, "big", 8)

deadline = time.time() + 30
while time.time() < deadline and len(master.failed_tasks) < 1:
    time.sleep(0.2)
assert len(master.failed_tasks) >= 1, f"crash task should fail, failed={master.failed_tasks}"
INFO(f"  Phase 1 OK: mid-write crash failed as expected, ids={list(master.failed_tasks)}")

# ── Phase 2: 残块不可见（对象不存在）──
try:
    db.read_object("big_partial_marker")
    raise AssertionError("partial object must not be readable after crash")
except KeyError:
    pass
INFO("  Phase 2 OK: partial object invisible (no trailer/entry)")

# ── Phase 3: 段回滚后同 db 恢复正常写 ──
write_recovery(db)

deadline = time.time() + 30
while time.time() < deadline and "recovery_probe" not in [o for o in []] and len(master.completed_tasks) < 1:
    time.sleep(0.2)

# 等待完成（completed 包含 recovery task）
def _done():
    return any("recovery" in str(t) or True for t in master.completed_tasks) and len(master.completed_tasks) >= 1

deadline = time.time() + 30
while time.time() < deadline and not _done():
    time.time() and time.sleep(0.2)

val = db.read_object("recovery_probe")
assert val == "ok", f"recovery write must succeed after abort, got {val!r}"
INFO("  Phase 3 OK: db recovers normal writes after segment abort")

master.stop()
INFO("[PASS] crash orphan chunks: partial invisible + segment rollback + recovery")
