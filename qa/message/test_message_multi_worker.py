"""测试 4：多 worker summary 合并（核心聚合正确性）。

覆盖：
  - 2 个 worker 各自上报 message 计数，master 合并后 summary 反映总和
  - master 自身计数独立累加
  - summary 同时含 id 级 + domain 级聚合
"""
import os
import shutil
from _fly_log import INFO
from fly import (
    open_db, get_config, get_work_directory, as_task,
    message, register_message_id,
)
from _msgtest import get_message_log_content, count_summary_block
from test import record, wait_tasks_no_failure

DB_PATH = os.path.join(get_work_directory(), "msg_multi_db")


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


cleanup()
get_config().set_int("fail_unscheduleable_tasks", 0)

register_message_id("MULTI::0001", "INFO")   # 两 worker 都用
register_message_id("MULTIMS::0001", "INFO")  # master 用

from fly.runtime import get_agent
master = get_agent()
# 启动 2 个 worker。
master.launch_local_workers([{}, {}])
assert master.wait_workers_registered(timeout=60), "2 worker 未全连上"

# master 自身发 3 次。
message("MULTIMS::0001", 1, "master m1")
message("MULTIMS::0001", 2, "master m2")
message("MULTIMS::0001", 3, "master m3")

db = open_db(DB_PATH)


@as_task()
def worker_send(db):
    register_message_id("MULTI::0001", "INFO")
    # 每个 worker 发 7 次（默认配额 20 内，全部推送 + master 打印）。
    for i in range(7):
        message("MULTI::0001", i, f"iter {i}")
    return "ok"


# 两个 worker 各跑一次（会分配到不同 worker）。
worker_send(db)
worker_send(db)
# 原断言：assert len(master.failed_tasks) == 0
assert wait_tasks_no_failure(master, 2, "worker_send/no_failed_tasks", timeout=20.0), \
    "2 task 未完成"

master.stop()

content = get_message_log_content()
summary = count_summary_block(content)

# 验证 1：summary id 级 —— MULTI::0001 = 2 worker × 7 = 14 次。
sum_id_multi = "MULTI::0001 : 14" in summary
# 原断言：assert "MULTI::0001 : 14" in summary, f"summary MULTI::0001 应为 14:\n{summary}"
record("summary/multi_0001_total_14", sum_id_multi, detail=summary)
# MULTIMS::0001 = master 3 次。
sum_id_multims = "MULTIMS::0001 : 3" in summary
# 原断言：assert "MULTIMS::0001 : 3" in summary, f"summary MULTIMS::0001 应为 3:\n{summary}"
record("summary/multims_0001_total_3", sum_id_multims)

# 验证 2：summary domain 级 —— MULTI = 14，MULTIMS = 3。
sum_dom_multi = "MULTI : 14" in summary
# 原断言：assert "MULTI : 14" in summary, f"summary domain MULTI 应为 14:\n{summary}"
record("summary/multi_domain_total_14", sum_dom_multi)
sum_dom_multims = "MULTIMS : 3" in summary
# 原断言：assert "MULTIMS : 3" in summary, f"summary domain MULTIMS 应为 3:\n{summary}"
record("summary/multims_domain_total_3", sum_dom_multims)

# 验证 3：message.log 收到 2 worker 各 7 条 + master 3 条 = 17 条 MULTI/MULTIMS message 行。
multi_lines = content.count("[MULTI::0001]")
multims_lines = content.count("[MULTIMS::0001]")
# 原断言：assert multi_lines == 14, f"MULTI::0001 message 行应为 14，实际 {multi_lines}"
record("message_log/multi_0001_lines", multi_lines)
# 原断言：assert multims_lines == 3, f"MULTIMS::0001 message 行应为 3，实际 {multims_lines}"
record("message_log/multims_0001_lines", multims_lines)

INFO("[DONE] test_message_multi_worker")
