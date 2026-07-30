"""测试 8：master 打印 domain 配额。

覆盖：
  - master 打印 domain 配额 N：同 domain 多 id 共享 N 次打印上限
  - 配额不影响 worker 触发计数（summary 仍反映 worker 实际触发次数）
"""
import os
import shutil
from _fly_log import INFO
from fly import (
    open_db, get_config, get_work_directory, as_task,
    message, register_message_id,
    set_master_print_domain_limit,
)
from _msgtest import wait_for, get_message_log_content

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
assert wait_for(lambda: master._agent.get_connection_count() >= 1)

db = open_db(DB_PATH)


@as_task()
def worker_send(db):
    register_message_id("MZDOM::0001", "INFO")
    register_message_id("MZDOM::0002", "INFO")
    # 发 3 条（domain MZDOM 共 3 次触发）。
    message("MZDOM::0001", 1, "d1")
    message("MZDOM::0001", 1, "d2")
    message("MZDOM::0002", 2, "d3")
    return "ok"


# 设置 master 打印 domain 配额 = 2（同 domain 共享 2 次打印）。
set_master_print_domain_limit("MZDOM", 2)

worker_send(db)
assert wait_for(lambda: len(master.completed_tasks) >= 1)
assert len(master.failed_tasks) == 0

master.stop()

content = get_message_log_content()

# 验证 1：master domain 打印配额 2 → message.log 只有 2 条 MZDOM message。
mzdom_total = content.count("[MZDOM::0001]") + content.count("[MZDOM::0002]")
assert mzdom_total == 2, f"master domain 配额=2，应有 2 条，实际 {mzdom_total}"

# 验证 2：summary 仍反映 worker 实际触发次数（MZDOM domain 共 3 次，含被 master 丢弃的）。
assert "MZDOM::0001 : 2" in content, f"summary MZDOM::0001 应为 2:\n{content}"
assert "MZDOM::0002 : 1" in content, f"summary MZDOM::0002 应为 1:\n{content}"
assert "MZDOM : 3" in content, f"summary domain MZDOM 应为 3:\n{content}"

INFO(f"[PASS] test_message_master_domain_quota")
