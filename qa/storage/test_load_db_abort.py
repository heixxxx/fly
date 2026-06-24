"""E2E test: 异常清理后的 load_db 验证。

验证事务化段标记的端到端持久化语义：
  Run 1: 正常 task 写 baseline，失败 task 写 dirty* 后 abort 清理（idx ABORT + data truncate）
  Run 2: load_db 重启，验证 baseline 可读、dirty* 不可读（ABORT 段被 load 丢弃）

每个 run 在独立进程中执行，模拟真实的进程重启。
"""
import os
import sys
import subprocess
import shutil

from fly import get_fly_binary, get_config

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(os.path.dirname(SCRIPT_DIR))
FLY_BIN = get_fly_binary()

DB_PATH = os.path.join(get_config().get_str("log_dir"), "db_abort")


def cleanup(path):
    if os.path.isdir(path):
        shutil.rmtree(path, ignore_errors=True)


def run_script(script_name, log_dir):
    script_path = os.path.join(SCRIPT_DIR, script_name)
    os.makedirs(log_dir, exist_ok=True)
    result = subprocess.run(
        [FLY_BIN, "--log-dir", log_dir, script_path],
        capture_output=True, text=True, timeout=120,
        cwd=PROJECT_ROOT, env={**os.environ, "FLY_DB_PATH": DB_PATH},
    )
    return result


cleanup(DB_PATH)

print("── Run 1: write baseline + abort dirty task ──", file=sys.stderr)
log_dir = os.path.join(SCRIPT_DIR, "logs", "load_db_abort")
r1 = run_script("load_db_abort_run1.py", os.path.join(log_dir, "run1"))
print(r1.stderr, file=sys.stderr)
if r1.returncode != 0:
    print(f"Run 1 FAILED (exit={r1.returncode})", file=sys.stderr)
    print(r1.stdout, file=sys.stderr)
    sys.exit(1)

assert os.path.isfile(os.path.join(DB_PATH, "_test_db_id")), "marker file should exist"
print("  Run 1 passed", file=sys.stderr)

print("── Run 2: load_db, verify dirty data absent ──", file=sys.stderr)
r2 = run_script("load_db_abort_run2.py", os.path.join(log_dir, "run2"))
print(r2.stderr, file=sys.stderr)
if r2.returncode != 0:
    print(f"Run 2 FAILED (exit={r2.returncode})", file=sys.stderr)
    print(r2.stdout, file=sys.stderr)
    sys.exit(1)

print("  Run 2 passed", file=sys.stderr)
print("[PASS] test_load_db_abort: abort cleanup + load_db verified", file=sys.stderr)
