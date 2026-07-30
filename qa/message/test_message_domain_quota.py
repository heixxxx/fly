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
from _msgtest import wait_for, get_message_log_content, get_worker_debug_log

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
assert wait_for(lambda: master._agent.get_connection_count() >= 1)

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
assert wait_for(lambda: len(master.completed_tasks) >= 1)
assert len(master.failed_tasks) == 0

master.stop()

content = get_message_log_content()
worker1_log = get_worker_debug_log(1)

# 验证 1：DOMQ::0001 每 id 独立配额 4 → 4 条进 message.log；DOMQ::0002 3 条。
assert content.count("[DOMQ::0001]") == 4, f"DOMQ::0001 应有 4 条，实际 {content.count('[DOMQ::0001]')}"
assert content.count("[DOMQ::0002]") == 3, f"DOMQ::0002 应有 3 条，实际 {content.count('[DOMQ::0002]')}"

# 验证 2：worker 本地 debug log 同步受控（DOMQ::0001 4 条，DOMQ::0002 3 条）。
assert worker1_log.count("[DOMQ::0001]") == 4, f"worker DOMQ::0001 应有 4 条，实际 {worker1_log.count('[DOMQ::0001]')}"
assert worker1_log.count("[DOMQ::0002]") == 3, f"worker DOMQ::0002 应有 3 条，实际 {worker1_log.count('[DOMQ::0002]')}"

# 验证 3：summary 反映总触发次数（DOMQ::0001 含超限丢弃共 5，DOMQ::0002 共 3，domain 合计 8）。
assert "DOMQ::0001 : 5" in content, f"summary DOMQ::0001 应为 5:\n{content}"
assert "DOMQ::0002 : 3" in content, f"summary DOMQ::0002 应为 3:\n{content}"
assert "DOMQ : 8" in content, f"summary domain DOMQ 应为 8:\n{content}"

INFO(f"[PASS] test_message_domain_quota")
