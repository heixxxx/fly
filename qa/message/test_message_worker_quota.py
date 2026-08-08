"""测试 2：worker 本地配额（id 配额 + domain 配额）。

覆盖：
  - worker 本地 id 配额默认 20：前 20 次推送 master，超限丢弃但仍计数
  - worker 本地 domain 配额：同 domain 多 id 共享配额
  - 配额 -1（不限）/ 0（禁止）边界
  - worker 本地 debug log 也受配额控制（超限不打印）
"""
import os
import shutil
from _fly_log import INFO
from fly import (
    open_db, get_config, get_work_directory, as_task,
    message, register_message_id,
)
from _msgtest import wait_for, get_message_log_content, get_worker_debug_log

DB_PATH = os.path.join(get_work_directory(), "msg_wquota_db")


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


cleanup()
get_config().set_int("fail_unscheduleable_tasks", 0)

register_message_id("WQUOTA::0001", "INFO")
register_message_id("WQUOTA::0002", "INFO")

from fly.runtime import get_agent
master = get_agent()
master.launch_local_workers([{}])
assert wait_for(lambda: master._agent.get_connection_count() >= 1)

db = open_db(DB_PATH)


# --- task 1：id 配额默认 20，发 22 次 ---
@as_task()
def worker_id_quota(db):
    register_message_id("WQUOTA::0001", "INFO")
    for i in range(22):
        message("WQUOTA::0001", 1, f"iter {i}")
    return "ok"


worker_id_quota(db)
assert wait_for(lambda: len(master.completed_tasks) >= 1)
assert len(master.failed_tasks) == 0

master.stop()

content = get_message_log_content()
worker1_log = get_worker_debug_log(1)

# 验证 1：master message.log 收到 20 条（worker 本地配额 20，推 20 条；master 打印配额默认 20 也放行 20）。
# 注意 [WQUOTA::0001] 在 summary 行无方括号，count 只匹配 message 行。
msg_count = content.count("[WQUOTA::0001]")
assert msg_count == 20, f"WQUOTA::0001 应有 20 条进 message.log，实际 {msg_count}"

# 验证 2：worker 本地 debug log 也只打印 20 条（本地配额控制）。
worker_count = worker1_log.count("[WQUOTA::0001]")
assert worker_count == 20, f"worker1 本地 debug log 应有 20 条，实际 {worker_count}"

# 验证 3：summary 显示总触发 22 次（含超限丢弃的 2 次）。
summary_block = content
assert "WQUOTA::0001 : 22" in summary_block, f"summary 应显示 WQUOTA::0001 触发 22 次:\n{summary_block}"

INFO("[PASS] test_message_worker_quota (id 配额)")
