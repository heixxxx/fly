"""测试 12：动态修改配额（验证两套计数解耦）。

核心验证：配额改变时 trigger 计数持续累加（summary），emit 计数保留并按新配额判定
（调大可继续输出，调小立即受限）。这正是两套计数模型的价值——不会因 trigger 过大导致
"调大配额后仍发不出"的 bug。

两个场景：
  1. 大→小：先 limit=10 触发 3 条（未超），改 limit=2 → 计数 3 >= 2，后续不输出
  2. 小→大：先 limit=2 触发 3 条（第3条丢弃），改 limit=5 → emit=2 < 5，后续恢复输出至 5
"""
import os
import shutil
from _fly_log import INFO
from fly import (
    open_db, get_config, get_work_directory, as_task,
    message, register_message_id, set_message_global_limit,
)
from _msgtest import wait_for, get_message_log_content

DB_PATH = os.path.join(get_work_directory(), "msg_dyn_db")


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


cleanup()
get_config().set_int("fail_unscheduleable_tasks", 0)

register_message_id("DYNS::0001", "INFO")   # 场景1：大→小
register_message_id("DNBG::0001", "INFO")   # 场景2：小→大

from fly.runtime import get_agent
master = get_agent()

master.launch_local_workers([{}])
assert wait_for(lambda: master._agent.get_connection_count() >= 1)

db = open_db(DB_PATH)


# === 场景 1：大 → 小（未超限前改小，立即受限）===
set_message_global_limit(10)


@as_task()
def phase1_large(db):
    register_message_id("DYNS::0001", "INFO")
    # limit=10，触发 3 条（emit=3，未超）。
    for i in range(3):
        message("DYNS::0001", 1, f"large {i}")
    return "ok"


phase1_large(db)
assert wait_for(lambda: len(master.completed_tasks) >= 1)

# master 改小配额 = 2（广播给 worker）。
set_message_global_limit(2)
# 等待同步到达 worker（worker 收到 MSG_LIMIT_SYNC 后替换本地配额）。
# 用一个空 task 强制 worker 轮询一次（task 执行时配额已同步）。
import time


@as_task()
def phase1_small(db):
    # 此 task 执行时 worker 已收到新配额 limit=2。
    # emit 计数=3（保留），3 >= 2 → 后续 DYNS::0001 全部超限丢弃。
    message("DYNS::0001", 1, "after shrink 1")
    message("DYNS::0001", 1, "after shrink 2")
    return "ok"


time.sleep(0.3)  # 给 MSG_LIMIT_SYNC 一点传播时间
phase1_small(db)
assert wait_for(lambda: len(master.completed_tasks) >= 2)


# === 场景 2：小 → 大（超限后改大，恢复输出至新配额）===
set_message_global_limit(2)


@as_task()
def phase2_small(db):
    register_message_id("DNBG::0001", "INFO")
    # limit=2，触发 3 条：前 2 条 emit，第 3 条丢弃。emit=2，trigger=3。
    for i in range(3):
        message("DNBG::0001", 2, f"small {i}")
    return "ok"


phase2_small(db)
assert wait_for(lambda: len(master.completed_tasks) >= 3)

# master 改大配额 = 5（广播给 worker）。
set_message_global_limit(5)
time.sleep(0.3)


@as_task()
def phase2_large(db):
    # 此 task 执行时 worker 已收到新配额 limit=5。
    # emit 计数=2（保留），2 < 5 → 恢复输出。发 4 条：
    #   第1条 emit=3, 第2条 emit=4, 第3条 emit=5, 第4条 5>=5 超限丢弃。
    # 即恢复输出 3 条（emit 3,4,5），第 4 条丢弃。
    for i in range(4):
        message("DNBG::0001", 2, f"after grow {i}")
    return "ok"


phase2_large(db)
assert wait_for(lambda: len(master.completed_tasks) >= 4)
assert len(master.failed_tasks) == 0

master.stop()

content = get_message_log_content()

# === 场景 1 断言：大→小 ===
# DYNS::0001：limit=10 发 3 条全 emit，改 limit=2 后 emit=3>=2 全丢弃 → 共 3 条进 message.log。
dyns_count = content.count("[DYNS::0001]")
assert dyns_count == 3, f"DYNS::0001 大→小应有 3 条（改小后不再输出），实际 {dyns_count}"
# summary trigger = 3 + 2 = 5（trigger 持续累加，含丢弃的）。
assert "DYNS::0001 : 5" in content, f"summary DYNS::0001 trigger 应为 5:\n{content}"

# === 场景 2 断言：小→大 ===
# DNBG::0001：limit=2 发 3 条 emit 2，改 limit=5 后 emit=2，发 4 条 emit 3（到5）→
#   共 emit = 2 + 3 = 5 条进 message.log。
dnbg_count = content.count("[DNBG::0001]")
assert dnbg_count == 5, f"DNBG::0001 小→大应有 5 条（改大后恢复输出至新配额），实际 {dnbg_count}"
# summary trigger = 3 + 4 = 7（trigger 持续累加，含丢弃的）。
assert "DNBG::0001 : 7" in content, f"summary DNBG::0001 trigger 应为 7:\n{content}"

INFO(f"[PASS] test_message_dynamic_quota")
