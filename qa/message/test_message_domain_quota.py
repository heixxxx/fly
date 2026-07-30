"""测试 6：domain 配额（worker 本地 + master 打印）。

覆盖：
  - worker 本地 domain 配额：同 domain 多 id 共享配额，超限丢弃但仍计数
  - master 打印 domain 配额：控制同 domain 推送 message 的打印上限
  - domain 配额与 id 配额同时生效（任一超限即丢弃）
"""
import os
import shutil
from _fly_log import INFO
from fly import (
    open_db, get_config, get_work_directory, as_task,
    message, register_message_id,
    set_message_domain_limit, set_master_print_domain_limit,
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

# master 打印 domain 配额设为很大（不限制），由 worker 本地 domain 配额主导测试。
set_master_print_domain_limit("DOMQ", -1)
master.launch_local_workers([{}])
assert wait_for(lambda: master._agent.get_connection_count() >= 1)

db = open_db(DB_PATH)


@as_task()
def worker_send(db):
    register_message_id("DOMQ::0001", "INFO")
    register_message_id("DOMQ::0002", "INFO")
    # worker 本地设 domain 配额 = 4：同 domain 下两个 id 共享 4 次。
    set_message_domain_limit("DOMQ", 4)
    # 发 DOMQ::0001 三次，DOMQ::0002 三次，共 6 次；前 4 次通过 domain 配额，后 2 次丢弃。
    message("DOMQ::0001", 1, "a1")
    message("DOMQ::0001", 1, "a2")
    message("DOMQ::0001", 1, "a3")
    message("DOMQ::0002", 2, "b1")
    message("DOMQ::0002", 2, "b2")  # domain 第 5 次 → 超限
    message("DOMQ::0002", 2, "b3")  # domain 第 6 次 → 超限
    return "ok"


worker_send(db)
assert wait_for(lambda: len(master.completed_tasks) >= 1)
assert len(master.failed_tasks) == 0

master.stop()

content = get_message_log_content()
worker1_log = get_worker_debug_log(1)

# 验证 1：domain 配额 4 → 只有 4 条进 message.log（worker 本地配额控制推送）。
total_dom = content.count("[DOMQ::0001]") + content.count("[DOMQ::0002]")
assert total_dom == 4, f"domain 配额=4，应有 4 条进 message.log，实际 {total_dom}"

# 验证 2：worker 本地 debug log 也只有 4 条（domain 配额同时控制本地打印）。
total_worker = worker1_log.count("[DOMQ::0001]") + worker1_log.count("[DOMQ::0002]")
assert total_worker == 4, f"worker 本地应有 4 条，实际 {total_worker}"

# 验证 3：summary 反映总触发 6 次（含超限丢弃的 2 次）。
assert "DOMQ::0001 : 3" in content, f"summary DOMQ::0001 应为 3:\n{content}"
assert "DOMQ::0002 : 3" in content, f"summary DOMQ::0002 应为 3:\n{content}"
assert "DOMQ : 6" in content, f"summary domain DOMQ 应为 6:\n{content}"

INFO(f"[PASS] test_message_domain_quota")
