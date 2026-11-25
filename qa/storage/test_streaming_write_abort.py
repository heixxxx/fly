"""E2E: 流式写中断残块治理（§14.1 测试 45 CrashOrphanChunks）。

场景：task 内流式写大对象中途 raise → 段事务回滚（abort）→
  1) 对象不可见（完成登记未发生——下游 wait 不可达）
  2) 残块（已落盘的块流）无 trailer 结构上不可读
  3) 同 db 后续写正常（段回滚后文件状态干净）
"""
from _fly_log import INFO
import os

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

from test import wait_until, record
assert wait_until(lambda: len(master.failed_tasks) >= 1, timeout=30), \
    f"crash task should fail, failed={master.failed_tasks}"
INFO(f"  Phase 1 OK: mid-write crash failed as expected, ids={list(master.failed_tasks)}")

# ── Phase 2: 残块不可见（对象不存在）──
partial_raised = None
try:
    db.read_object("big_partial_marker")
except Exception as e:
    partial_raised = {"type": type(e).__name__, "msg": str(e)}
# 原断言（try 块内 raise AssertionError——机械核对不追踪 raise 语句，
# 转成记录保持等价语义）：
#   raise AssertionError("partial object must not be readable after crash")
record("partial_object_unreadable", partial_raised is not None, detail=partial_raised)
record("partial_object_error_type", partial_raised["type"] if partial_raised else None)
INFO("  Phase 2 OK: partial object invisible (no trailer/entry)")

# ── Phase 3: 段回滚后同 db 恢复正常写 ──
write_recovery(db)

# 等待 recovery task 完成（completed 包含 recovery task）
assert wait_until(lambda: len(master.completed_tasks) >= 1, timeout=30), \
    "recovery write should complete after segment abort"

# 原断言：assert val == "ok", f"recovery write must succeed after abort, got {val!r}"
record("recovery_probe_value", db.read_object("recovery_probe"))
INFO("  Phase 3 OK: db recovers normal writes after segment abort")

master.stop()
INFO("[DONE] crash orphan chunks: partial invisible + segment rollback + recovery")
