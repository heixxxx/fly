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

import fly
from fly import (
    get_task_error, put_cache, get_cache, has_cache, remove_cache, clear_cache,
    get_fly_binary, get_config,
)
from fly.runtime import get_agent

master = get_agent()
master.start()

# ── 1. 模块级 __getattr__ 透传（同一 master 视角）────────────────────
assert fly.pending_tasks == master.pending_tasks
assert fly.running_tasks == master.running_tasks
assert fly.completed_tasks == master.completed_tasks
assert fly.failed_tasks == master.failed_tasks
assert isinstance(fly.port, int) and fly.port == master.port and fly.port > 0
INFO("[PASS] surface passthrough: task lists + port")

# ── 2. 不存在符号 → AttributeError（__getattr__ 兜底分支）───────────
try:
    fly.definitely_not_an_api
    raise AssertionError("unknown symbol must raise AttributeError")
except AttributeError as e:
    assert "definitely_not_an_api" in str(e)
INFO("[PASS] surface __getattr__ unknown symbol raises")

# ── 3. get_task_error：不存在的 task id → 空串 ──────────────────────
assert get_task_error(0) == "", \
    f"unknown task error should be empty, got {get_task_error(0)!r}"
INFO("[PASS] get_task_error(0) == ''")

# ── 4. 本地 cache 往返 ───────────────────────────────────────────────
clear_cache()
assert has_cache("k") is False
assert get_cache("k") is None
assert get_cache("k", default="dflt") == "dflt", "miss 应返回显式 default"

put_cache("k", {"v": [1, 2, 3]})
assert has_cache("k") is True
assert get_cache("k") == {"v": [1, 2, 3]}

put_cache("k", "overwritten")  # 同 key 覆盖
assert get_cache("k") == "overwritten"

remove_cache("k")
assert has_cache("k") is False
try:
    remove_cache("k")
    raise AssertionError("remove_cache on missing key must raise KeyError")
except KeyError:
    pass

put_cache("another", 42)
clear_cache()
assert has_cache("another") is False and has_cache("k") is False
INFO("[PASS] put/get/has/remove/clear_cache roundtrip")

# ── 5. get_fly_binary：返回可执行路径（fly 进程内 sys._fly_binary 注入）──
bin_path = get_fly_binary()
assert os.path.isfile(bin_path), f"fly binary must exist: {bin_path}"
assert os.access(bin_path, os.X_OK), f"fly binary must be executable: {bin_path}"
INFO(f"[PASS] get_fly_binary -> {bin_path}")

master.stop()
INFO("[PASS] test_fly_surface_api")
