"""fly 表层 API 透传 + 本地 cache 往返 + get_fly_binary（master 进程内）。

覆盖（2026-09 覆盖率批次 14 项之 2，qa 部分）：
  - fly.completed_tasks / pending_tasks / running_tasks / failed_tasks / port
    模块级 __getattr__ 透传（与 master agent 视角一致）
  - fly.<不存在符号> → AttributeError
  - get_task_error(不存在 id) → 空串
  - put/get/has/remove/clear_cache 往返 + miss 语义
  - get_fly_binary() 返回可执行路径
"""
import os
import stat

from _fly_log import INFO

from test import record

import fly
from fly import (
    get_task_error, put_cache, get_cache, has_cache, remove_cache, clear_cache,
    get_fly_binary, get_config,
)
from fly.runtime import get_agent

master = get_agent()
master.start()

# ── 1. 模块级 __getattr__ 透传（同一 master 视角）────────────────────
pending_eq = fly.pending_tasks == master.pending_tasks
# 原断言：assert fly.pending_tasks == master.pending_tasks
record("passthrough/pending_tasks_equal", pending_eq, detail=pending_eq)
running_eq = fly.running_tasks == master.running_tasks
# 原断言：assert fly.running_tasks == master.running_tasks
record("passthrough/running_tasks_equal", running_eq, detail=running_eq)
completed_eq = fly.completed_tasks == master.completed_tasks
# 原断言：assert fly.completed_tasks == master.completed_tasks
record("passthrough/completed_tasks_equal", completed_eq, detail=completed_eq)
failed_eq = fly.failed_tasks == master.failed_tasks
# 原断言：assert fly.failed_tasks == master.failed_tasks
record("passthrough/failed_tasks_equal", failed_eq, detail=failed_eq)
port_is_int = isinstance(fly.port, int)
port_matches = fly.port == master.port
port_positive = fly.port > 0
# 原断言：assert isinstance(fly.port, int) and fly.port == master.port and fly.port > 0
record("passthrough/port_ok", port_is_int and port_matches and port_positive,
       detail={"is_int": port_is_int, "matches_master": port_matches,
               "positive": port_positive})
INFO("[DONE] surface passthrough: task lists + port")

# ── 2. 不存在符号 → AttributeError（__getattr__ 兜底分支）───────────
raised = None
msg_mentions = False
try:
    fly.definitely_not_an_api
    raise AssertionError("unknown symbol must raise AttributeError")
except AttributeError as e:
    raised = {"type": type(e).__name__, "msg": str(e)}
    msg_mentions = "definitely_not_an_api" in str(e)
# 原断言：assert "definitely_not_an_api" in str(e)
record("getattr_unknown/error_type", raised["type"] if raised else None)
record("getattr_unknown/msg_mentions_symbol", msg_mentions, detail=raised)
INFO("[DONE] surface __getattr__ unknown symbol raises")

# ── 3. get_task_error：不存在的 task id → 空串 ──────────────────────
unknown_err = get_task_error(0)
# 原断言：assert get_task_error(0) == "", \
record("task_error/unknown_id_empty", unknown_err)
INFO("[DONE] get_task_error(0) == ''")

# ── 4. 本地 cache 往返 ───────────────────────────────────────────────
clear_cache()
k_absent = has_cache("k") is False
# 原断言：assert has_cache("k") is False
record("cache_miss/has_k_is_false", k_absent, detail=k_absent)
miss_val = get_cache("k")
# 原断言：assert get_cache("k") is None
record("cache_miss/get_k_value", miss_val)
miss_default = get_cache("k", default="dflt")
# 原断言：assert get_cache("k", default="dflt") == "dflt", "miss 应返回显式 default"
record("cache_miss/get_k_default", miss_default)

put_cache("k", {"v": [1, 2, 3]})
k_present = has_cache("k") is True
# 原断言：assert has_cache("k") is True
record("cache_roundtrip/has_k_is_true", k_present, detail=k_present)
k_value = get_cache("k")
# 原断言：assert get_cache("k") == {"v": [1, 2, 3]}
record("cache_roundtrip/get_k_value", k_value)

put_cache("k", "overwritten")  # 同 key 覆盖
k_overwritten = get_cache("k")
# 原断言：assert get_cache("k") == "overwritten"
record("cache_roundtrip/get_k_overwritten", k_overwritten)

remove_cache("k")
k_absent_again = has_cache("k") is False
# 原断言：assert has_cache("k") is False
record("cache_remove/has_k_is_false", k_absent_again, detail=k_absent_again)
try:
    remove_cache("k")
    raise AssertionError("remove_cache on missing key must raise KeyError")
except KeyError:
    pass

put_cache("another", 42)
clear_cache()
another_absent = has_cache("another") is False
k_absent_after_clear = has_cache("k") is False
# 原断言：assert has_cache("another") is False and has_cache("k") is False
record("clear_cache/both_absent", another_absent and k_absent_after_clear,
       detail={"another": another_absent, "k": k_absent_after_clear})
INFO("[DONE] put/get/has/remove/clear_cache roundtrip")

# ── 5. get_fly_binary：返回可执行路径（fly 进程内 sys._fly_binary 注入）──
bin_path = get_fly_binary()
bin_exists = os.path.isfile(bin_path)
# 原断言：assert os.path.isfile(bin_path), f"fly binary must exist: {bin_path}"
record("fly_binary/exists", bin_exists, detail=bin_path)
bin_executable = os.access(bin_path, os.X_OK)
# 原断言：assert os.access(bin_path, os.X_OK), f"fly binary must be executable: {bin_path}"
record("fly_binary/executable", bin_executable, detail=bin_path)
INFO(f"[DONE] get_fly_binary -> {bin_path}")

master.stop()
INFO("[DONE] test_fly_surface_api")
