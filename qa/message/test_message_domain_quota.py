"""测试 6：domain 配额（worker 本地 + master 打印）。

覆盖：
  - worker 本地 domain 配额：语义与 global 相同（每 id 独立计数），仅对该 domain 生效，覆盖 global。
    两个 id 各自独立，互不影响（不是跨 id 共享）。
  - master 打印 domain 配额：控制同 domain 推送 message 的打印上限（同样每 id 独立）。
"""
import os
import shutil
from _fly_log import INFO
from fly import (
    open_db, get_config, get_work_directory, as_task,
    message, register_message_id,
    set_message_domain_limit,
)
from _msgtest import get_message_log_content, get_worker_debug_log
from test import record, wait_tasks_no_failure

DB_PATH = os.path.join(get_work_directory(), "msg_domquota_db")


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


cleanup()
get_config().set_int("fail_unscheduleable_tasks", 0)

register_message_id("DOMQ::0001", "INFO")
register_message_id("DOMQ::0002", "INFO")

from fly.runtime import get_agent
master = get_agent()

master.launch_local_workers([{}])
assert master.wait_workers_registered(timeout=60)

db = open_db(DB_PATH)


@as_task()
def worker_send(db):
    register_message_id("DOMQ::0001", "INFO")
    register_message_id("DOMQ::0002", "INFO")
    # worker 本地设 domain 配额 = 4：语义与 global 相同（每 id 独立计数），
    # 仅对该 domain 内 id 生效，覆盖 global 默认。不是跨 id 共享。
    set_message_domain_limit("DOMQ", 4)
    # DOMQ::0001 发 5 次 → 该 id 独立配额 4，前 4 次通过、第 5 次丢弃。
    message("DOMQ::0001", 1, "a1")
    message("DOMQ::0001", 1, "a2")
    message("DOMQ::0001", 1, "a3")
    message("DOMQ::0001", 1, "a4")
    message("DOMQ::0001", 1, "a5")  # 该 id 第 5 次 → 超限
    # DOMQ::0002 发 3 次 → 该 id 独立配额 4，3 < 4 全通过（与 DOMQ::0001 互不影响）。
    message("DOMQ::0002", 2, "b1")
    message("DOMQ::0002", 2, "b2")
    message("DOMQ::0002", 2, "b3")
    return "ok"


worker_send(db)
# 原断言：assert len(master.failed_tasks) == 0
assert wait_tasks_no_failure(master, 1, "worker_send/no_failed_tasks", timeout=20.0), \
    "task 未完成"

master.stop()

content = get_message_log_content()
worker1_log = get_worker_debug_log(1)

# 验证 1：DOMQ::0001 每 id 独立配额 4 → 4 条进 message.log；DOMQ::0002 3 条。
domq1_count = content.count("[DOMQ::0001]")
# 原断言：assert content.count("[DOMQ::0001]") == 4, f"DOMQ::0001 应有 4 条，实际 {content.count('[DOMQ::0001]')}"
record("message_log/domq_0001_count", domq1_count)
domq2_count = content.count("[DOMQ::0002]")
# 原断言：assert content.count("[DOMQ::0002]") == 3, f"DOMQ::0002 应有 3 条，实际 {content.count('[DOMQ::0002]')}"
record("message_log/domq_0002_count", domq2_count)

# 验证 2：worker 本地 debug log 同步受控（DOMQ::0001 4 条，DOMQ::0002 3 条）。
w_domq1_count = worker1_log.count("[DOMQ::0001]")
# 原断言：assert worker1_log.count("[DOMQ::0001]") == 4, f"worker DOMQ::0001 应有 4 条，实际 {worker1_log.count('[DOMQ::0001]')}"
record("worker_log/domq_0001_count", w_domq1_count)
w_domq2_count = worker1_log.count("[DOMQ::0002]")
# 原断言：assert worker1_log.count("[DOMQ::0002]") == 3, f"worker DOMQ::0002 应有 3 条，实际 {worker1_log.count('[DOMQ::0002]')}"
record("worker_log/domq_0002_count", w_domq2_count)

# 验证 3：summary 反映总触发次数（DOMQ::0001 含超限丢弃共 5，DOMQ::0002 共 3，domain 合计 8）。
sum_domq1 = "DOMQ::0001 : 5" in content
# 原断言：assert "DOMQ::0001 : 5" in content, f"summary DOMQ::0001 应为 5:\n{content}"
record("summary/domq_0001_total_5", sum_domq1, detail=content)
sum_domq2 = "DOMQ::0002 : 3" in content
# 原断言：assert "DOMQ::0002 : 3" in content, f"summary DOMQ::0002 应为 3:\n{content}"
record("summary/domq_0002_total_3", sum_domq2)
sum_domq = "DOMQ : 8" in content
# 原断言：assert "DOMQ : 8" in content, f"summary domain DOMQ 应为 8:\n{content}"
record("summary/domq_domain_total_8", sum_domq)

INFO("[DONE] test_message_domain_quota")
