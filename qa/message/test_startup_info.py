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
from test import record

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
master_in_log = "Fly Startup Info (master)" in content
# 原断言：assert "Fly Startup Info (master)" in content, "master 启动信息未进 message.log"
record("startup/master_in_message_log", master_in_log, detail=content)
sec_binary = "--- Binary ---" in content
# 原断言：assert "--- Binary ---" in content
record("startup/section_binary", sec_binary, detail=content)
sec_machine = "--- Machine ---" in content
# 原断言：assert "--- Machine ---" in content
record("startup/section_machine", sec_machine)
sec_network = "--- Network ---" in content
# 原断言：assert "--- Network ---" in content
record("startup/section_network", sec_network)
sec_runtime = "--- Runtime ---" in content
# 原断言：assert "--- Runtime ---" in content
record("startup/section_runtime", sec_runtime)

# 验证 2：关键字段非空（出现在 message.log）。合并字段：build @ time (commit)、user@host、ip:port。
# commit 不硬编码具体值（随提交变化），只校验字段存在且非 unknown。
commit_present = "commit" in content
# 原断言：assert "commit" in content, "commit 缺失"
record("startup/commit_field_present", commit_present, detail=content)
commit_resolved = "unknown" not in content.split("build")[1].split("\n")[0]
# 原断言：assert "unknown" not in content.split("build")[1].split("\n")[0], "commit 解析失败"
record("startup/commit_not_unknown", commit_resolved,
       detail=content.split("build")[1].split("\n")[0] if commit_present else None)
build_field = "build" in content and "@" in content
# 原断言：assert "build" in content and "@" in content, "build (type @ time) 缺失"
record("startup/build_type_at_time", build_field)
host_field = "host" in content and "@" in content
# 原断言：assert "host" in content and "@" in content, "host (user@host) 缺失"
record("startup/host_user_at_host", host_field)
pid_field = "pid" in content
# 原断言：assert "pid" in content, "pid 缺失"
record("startup/pid_field_present", pid_field)
log_field = "log" in content
# 原断言：assert "log" in content, "log path 缺失"
record("startup/log_path_present", log_field)
msg_log_field = "msg log" in content
# 原断言：assert "msg log" in content, "message log path 缺失"
record("startup/msg_log_path_present", msg_log_field)
started_field = "started" in content
# 原断言：assert "started" in content, "start time 缺失"
record("startup/started_field_present", started_field)
# binary 应为绝对路径。
binary_field = "binary" in content and "/fly" in content
# 原断言：assert "binary" in content and "/fly" in content, "binary path 缺失或非绝对路径"
record("startup/binary_absolute_path", binary_field)
# listen 应为 ip:port 形式。
listen_field = "listen" in content and ":" in content
# 原断言：assert "listen" in content and ":" in content, "listen (ip:port) 缺失"
record("startup/listen_ip_port", listen_field)

# 验证 3：master 启动信息也写本地 debug log。
master_in_debug = "Fly Startup Info (master)" in master_log
# 原断言：assert "Fly Startup Info (master)" in master_log, "master 启动信息未写本地 debug log"
record("debug_log/master_startup_info", master_in_debug, detail=master_log[-2000:])

# 验证 4：worker 也打印了启动信息（本地 debug log），角色为 worker。
worker_in_debug = "Fly Startup Info (worker)" in worker1_log
# 原断言：assert "Fly Startup Info (worker)" in worker1_log, "worker 启动信息未写本地 debug log"
record("worker_log/startup_info", worker_in_debug, detail=worker1_log[-2000:])

# 验证 5：worker 的 FLY::0000 没有发送给 master —— message.log 不含 worker 的启动信息。
no_worker_in_log = "Fly Startup Info (worker)" not in content
# 原断言：assert "Fly Startup Info (worker)" not in content, \
record("message_log/no_worker_startup_info", no_worker_in_log)

# 验证 6：FLY::0000 豁免配额 —— 不计入 summary 的 id 级计数。
# summary 块里不应出现 "FLY::0000"（说明没进 MessageRegistry 触发计数）。
fly0000_exempt = "FLY::0000" not in summary
# 原断言：assert "FLY::0000" not in summary, f"FLY::0000 应豁免配额，不应进 summary:\n{summary}"
record("summary/fly_0000_exempt", fly0000_exempt, detail=summary)

INFO("[DONE] test_startup_info")
