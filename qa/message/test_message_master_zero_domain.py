"""测试 8：master 打印 domain 配额。

覆盖：
  - master 打印 domain 配额 N：语义与 global 相同（每 id 独立计数），仅对该 domain 生效。
    每个 id 各自独立 N 次打印上限，不跨 id 共享。
  - 配额不影响 worker 触发计数（summary 仍反映 worker 实际触发次数）。
"""
import os
import shutil
from _fly_log import INFO
from fly import (
    open_db, get_config, get_work_directory, as_task,
    message, register_message_id,
    set_message_domain_limit,
)
from _msgtest import get_message_log_content
from test import record, wait_tasks_no_failure

DB_PATH = os.path.join(get_work_directory(), "msg_mdom_db")


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


cleanup()
get_config().set_int("fail_unscheduleable_tasks", 0)

register_message_id("MZDOM::0001", "INFO")
register_message_id("MZDOM::0002", "INFO")

from fly.runtime import get_agent
master = get_agent()

master.launch_local_workers([{}])
assert master.wait_workers_registered(timeout=60)

db = open_db(DB_PATH)


# 统一配额 = 2：同时控 worker 发送 + master 打印（domain 层，每 id 独立 2 次）。
set_message_domain_limit("MZDOM", 2)


@as_task()
def worker_send(db):
    register_message_id("MZDOM::0001", "INFO")
    register_message_id("MZDOM::0002", "INFO")
    # MZDOM::0001 发 3 次：配额 2（每 id 独立）→ 前 2 条 emit，第 3 条丢弃。
    message("MZDOM::0001", 1, "d1")
    message("MZDOM::0001", 1, "d2")
    message("MZDOM::0001", 1, "d3")  # 该 id 第 3 次 → 超限丢弃（trigger 仍计）
    # MZDOM::0002 发 2 次：独立配额 2，2 ≤ 2 全 emit（与 MZDOM::0001 互不影响）。
    message("MZDOM::0002", 2, "e1")
    message("MZDOM::0002", 2, "e2")
    return "ok"


worker_send(db)
# 原断言：assert len(master.failed_tasks) == 0
assert wait_tasks_no_failure(master, 1, "worker_send/no_failed_tasks", timeout=20.0)

master.stop()

content = get_message_log_content()

# 验证 1：MZDOM::0001 每 id 独立配额 2 → 2 条进 message.log；MZDOM::0002 全 2 条。
mz1_count = content.count("[MZDOM::0001]")
# 原断言：assert content.count("[MZDOM::0001]") == 2, f"MZDOM::0001 应有 2 条，实际 {content.count('[MZDOM::0001]')}"
record("message_log/mzdom_0001_count", mz1_count)
mz2_count = content.count("[MZDOM::0002]")
# 原断言：assert content.count("[MZDOM::0002]") == 2, f"MZDOM::0002 应有 2 条，实际 {content.count('[MZDOM::0002]')}"
record("message_log/mzdom_0002_count", mz2_count)

# 验证 2：summary 反映 worker 实际触发次数（配额不影响 trigger 计数）。
mz1_trigger = "MZDOM::0001 : 3" in content
# 原断言：assert "MZDOM::0001 : 3" in content, f"summary MZDOM::0001 应为 3:\n{content}"
record("summary/mzdom_0001_trigger_3", mz1_trigger, detail=content)
mz2_trigger = "MZDOM::0002 : 2" in content
# 原断言：assert "MZDOM::0002 : 2" in content, f"summary MZDOM::0002 应为 2:\n{content}"
record("summary/mzdom_0002_trigger_2", mz2_trigger)
mz_domain_trigger = "MZDOM : 5" in content
# 原断言：assert "MZDOM : 5" in content, f"summary domain MZDOM 应为 5:\n{content}"
record("summary/mzdom_domain_trigger_5", mz_domain_trigger)

INFO("[DONE] test_message_master_domain_quota")
