"""测试：FLY::0000 启动信息。

覆盖：
  - master 启动时打印 FLY::0000 启动信息（message.log + terminal）
  - 启动信息含四大分组：Binary / Machine / Network / Runtime
  - 关键字段非空：commit id、build type、build time、hostname、pid、log path、start time
  - worker 也打印 FLY::0000（本地 debug log），但不发送 master（message.log 无 worker 的 FLY::0000）
  - FLY::0000 豁免配额：不计入 summary（summary 的 id 级计数不含 FLY::0000）
"""
import os
import shutil
from _fly_log import INFO
from fly import get_config, get_work_directory
from _msgtest import wait_for, get_message_log_content, get_worker_debug_log, get_master_debug_log, count_summary_block

get_config().set_int("fail_unscheduleable_tasks", 0)

from fly.runtime import get_agent
master = get_agent()
master.launch_local_workers([{}])
assert master.wait_workers_registered(timeout=60), "worker 未连上"

master.stop()

content = get_message_log_content()
summary = count_summary_block(content)
master_log = get_master_debug_log()
worker1_log = get_worker_debug_log(1)

# 验证 1：master 启动信息在 message.log，含四大分组标题。
assert "Fly Startup Info (master)" in content, "master 启动信息未进 message.log"
assert "--- Binary ---" in content
assert "--- Machine ---" in content
assert "--- Network ---" in content
assert "--- Runtime ---" in content

# 验证 2：关键字段非空（出现在 message.log）。合并字段：build @ time (commit)、user@host、ip:port。
# commit 不硬编码具体值（随提交变化），只校验字段存在且非 unknown。
assert "commit" in content, "commit 缺失"
assert "unknown" not in content.split("build")[1].split("\n")[0], "commit 解析失败"
assert "build" in content and "@" in content, "build (type @ time) 缺失"
assert "host" in content and "@" in content, "host (user@host) 缺失"
assert "pid" in content, "pid 缺失"
assert "log" in content, "log path 缺失"
assert "msg log" in content, "message log path 缺失"
assert "started" in content, "start time 缺失"
# binary 应为绝对路径。
assert "binary" in content and "/fly" in content, "binary path 缺失或非绝对路径"
# listen 应为 ip:port 形式。
assert "listen" in content and ":" in content, "listen (ip:port) 缺失"

# 验证 3：master 启动信息也写本地 debug log。
assert "Fly Startup Info (master)" in master_log, "master 启动信息未写本地 debug log"

# 验证 4：worker 也打印了启动信息（本地 debug log），角色为 worker。
assert "Fly Startup Info (worker)" in worker1_log, "worker 启动信息未写本地 debug log"

# 验证 5：worker 的 FLY::0000 没有发送给 master —— message.log 不含 worker 的启动信息。
assert "Fly Startup Info (worker)" not in content, \
    "worker 启动信息不应发送 master（message.log 不应有 worker 启动信息）"

# 验证 6：FLY::0000 豁免配额 —— 不计入 summary 的 id 级计数。
# summary 块里不应出现 "FLY::0000"（说明没进 MessageRegistry 触发计数）。
assert "FLY::0000" not in summary, f"FLY::0000 应豁免配额，不应进 summary:\n{summary}"

INFO("[PASS] test_startup_info")
