"""测试 13：summary 计数精确性（不双算 + master 丢弃不影响 worker trigger）。

核心验证 summary 的两条不变量：
  1. master 打印配额丢弃的 message，summary 仍按 worker trigger 准确计数（不漏算）。
     场景：worker 推 10 条（trigger=10），master 打印配额=5 只打印 5 条（丢弃 5 条），
     summary 应显示 worker trigger=10（不是被丢弃后的 5，也不是双算的 15）。
  2. master 自身 message 的 trigger 进 summary，且与 worker trigger 合并精确。
"""
import os
import shutil
from _fly_log import INFO
from fly import (
    open_db, get_config, get_work_directory, as_task,
    message, register_message_id, set_message_global_limit,
)
from _msgtest import wait_for, get_message_log_content, count_summary_block

DB_PATH = os.path.join(get_work_directory(), "msg_sum_db")


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


cleanup()
get_config().set_int("fail_unscheduleable_tasks", 0)

register_message_id("SUMW::0001", "INFO")    # worker 用（master 丢弃但 trigger 仍计）
register_message_id("SUMMS::0001", "INFO")   # master 自身用

from fly.runtime import get_agent
master = get_agent()

# 配额 = 5：同时控 worker 发送(5) + master 打印(5)。
# 注意：worker 发送也受限 5，所以 worker 实际推 5 条。为制造「master 丢弃」场景，
# 需要 worker 推的条数 > master 打印配额。但统一配额下二者相同，无法制造 master 额外丢弃。
# 改用 2 个 worker：各推 5 条（worker 配额内），master 打印配额=5 → master 总量限流，
# 2 worker 共 10 条推送，master 只打印 5 条（丢弃 5 条）。
set_message_global_limit(5)

master.launch_local_workers([{}, {}])
assert wait_for(lambda: master._agent.get_connection_count() >= 2), "2 worker 未全连上"

# master 自身发 2 条（master 自己的 Registry，配额内）。
message("SUMMS::0001", 1, "master s1")
message("SUMMS::0001", 2, "master s2")

db = open_db(DB_PATH)


@as_task()
def worker_send(db):
    register_message_id("SUMW::0001", "INFO")
    # 配额=5（已同步），每个 worker 发 5 条（worker emit 配额内，全推送）。
    for i in range(5):
        message("SUMW::0001", 1, f"worker iter {i}")
    return "ok"


# 两个 worker 各跑一次。
worker_send(db)
worker_send(db)
assert wait_for(lambda: len(master.completed_tasks) >= 2), "2 task 未完成"
assert len(master.failed_tasks) == 0

master.stop()

content = get_message_log_content()
summary = count_summary_block(content)

# === 验证 1（核心）：master 丢弃不影响 worker trigger 计数 ===
# 2 worker 各 trigger 5 = 10。master 打印配额=5 只打印 5 条（丢弃 5 条），
# 但 summary 应显示 worker trigger 总和 = 10（不是被 master 丢弃后的 5，也不是双算）。
assert "SUMW::0001 : 10" in summary, (
    f"summary SUMW::0001 应为 10（2 worker × 5 trigger，master 丢弃不影响计数）:\n{summary}")

# domain 级：SUMW = 10。
assert "SUMW : 10" in summary, f"summary domain SUMW 应为 10:\n{summary}"

# === 验证 2：master 自身 message trigger 进 summary ===
# master 自身发 2 条，进 master 自己的 Registry trigger 计数。
assert "SUMMS::0001 : 2" in summary, f"summary SUMMS::0001 应为 2（master 自身）:\n{summary}"
assert "SUMMS : 2" in summary, f"summary domain SUMMS 应为 2:\n{summary}"

# === 验证 3：master 打印配额确实丢弃了（message.log 行数 < trigger 总和）===
# master 打印配额=5，2 worker 共推 10 条 → message.log 只 5 条 SUMW 行（master 丢弃 5 条）。
sumw_lines = content.count("[SUMW::0001]")
assert sumw_lines == 5, (
    f"master 打印配额=5，message.log 应只 5 条 SUMW 行（丢弃 5 条），实际 {sumw_lines}")

# master 自身 2 条不受 master 打印配额丢弃（master 自己 Registry 配额=5 内）。
summs_lines = content.count("[SUMMS::0001]")
assert summs_lines == 2, f"master 自身 2 条应全打印，实际 {summs_lines}"

INFO(f"[PASS] test_message_summary_accuracy")
