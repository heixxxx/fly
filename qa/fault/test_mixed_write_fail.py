"""E2E test: 大对象+多对象跨文件失败 → load_db + restart 端到端验证。

场景覆盖三个单测场景的端到端组合：
  - 小对象聚合在第一个 .dat
  - 大对象(>1MB)触发 rollover 到新 .dat
  - 大对象后的小对象落在新 .dat
  task 失败 → abort 清理（删新 .dat + truncate 原 .dat + idx ABORT）

Run 1: 提交 mixed_size_write_fail → 失败 → 持久化 → 退出
Run 2: load_db 验证脏数据不恢复 + restart 重跑验证成功 + 数据正确
"""
import os
import sys
import subprocess
import shutil

from fly import get_fly_binary, get_config

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(os.path.dirname(SCRIPT_DIR))
FLY_BIN = get_fly_binary()

DB_PATH = os.path.join(get_config().get_str("log_dir"), "db_mixed")


def cleanup(path):
    if os.path.isdir(path):
        shutil.rmtree(path, ignore_errors=True)


def run_script(script_name, log_dir, extra_env=None):
    script_path = os.path.join(SCRIPT_DIR, script_name)
    os.makedirs(log_dir, exist_ok=True)
    env = {**os.environ, "FLY_DB_PATH": DB_PATH}
    if extra_env:
        env.update(extra_env)
    result = subprocess.run(
        [FLY_BIN, "--log-dir", log_dir, script_path],
        capture_output=True, text=True, timeout=120,
        cwd=PROJECT_ROOT, env=env,
    )
    return result


cleanup(DB_PATH)

SHARED_LOG = os.path.join(SCRIPT_DIR, "test_mixed_write_fail")

# ── Run 1: 提交 task，写入混合大小对象后失败 ──
print("── Run 1: mixed write + fail + persist ──", file=sys.stderr)
r1 = run_script("mixed_fail_run1.py", SHARED_LOG, extra_env={"FLY_MIXED_FAIL": "1"})
print(r1.stderr, file=sys.stderr)
if r1.returncode != 0:
    print(f"Run 1 FAILED (exit={r1.returncode})", file=sys.stderr)
    print(r1.stdout, file=sys.stderr)
    sys.exit(1)
print("  Run 1 passed", file=sys.stderr)

# ── Run 2: load_db + restart → 验证成功 ──
print("── Run 2: load_db + restart ──", file=sys.stderr)
r2 = run_script("mixed_fail_run2.py", SHARED_LOG, extra_env={"FLY_MIXED_FAIL": "0"})
print(r2.stderr, file=sys.stderr)
if r2.returncode != 0:
    print(f"Run 2 FAILED (exit={r2.returncode})", file=sys.stderr)
    print(r2.stdout, file=sys.stderr)
    sys.exit(1)
print("  Run 2 passed", file=sys.stderr)

print("[PASS] test_mixed_write_fail: cross-file abort + load_db + restart verified",
      file=sys.stderr)
