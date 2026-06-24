"""E2E test: 依赖链中途失败 → 持久化 → 跨进程 restart 恢复。

场景：
  Run 1: 提交 5 阶段依赖链（stage0→1→2→3→4），FLY_FAIL_AT=2 使 stage2 失败。
         stage3/4 因依赖 stage2/result 被清理而连锁失败。
         全部 failed task 持久化到 failed_tasks.bin，master 退出。
  Run 2: load_db 恢复 stage0/1 数据，restart_failed_tasks 重跑 stage2/3/4。
         FLY_FAIL_AT 未设（不失败），全部成功，数据正确，无脏残留。

验证点：
  1. 中途失败的 task 写入的脏对象被清理（ABORT + data truncate）
  2. 连锁失败的 task 被持久化（failed_tasks.bin）
  3. restart 后按依赖正确重调度，无重复写/拒绝
  4. 全链数据正确
"""
import os
import sys
import subprocess
import shutil

from fly import get_fly_binary, get_config

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(os.path.dirname(SCRIPT_DIR))
FLY_BIN = get_fly_binary()

DB_PATH = os.path.join(get_config().get_str("log_dir"), "db_chain")


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

# run1 和 run2 共用同一个 log_dir。fly 的 log 系统会递增创建 .N 变体：
# run1（第一次运行）落在原名，run2 落在 .1。run2 固定读原名/failed_tasks.bin。
SHARED_LOG = os.path.join(SCRIPT_DIR, "test_chain_failure_restart")

# ── Run 1: 提交 DAG，node7 失败，下游 sleep 后读不到文件，连锁失败 ──
print("── Run 1: submit DAG, node7 fails, downstream cascades ──", file=sys.stderr)
r1 = run_script("chain_fail_run1.py", SHARED_LOG, extra_env={
    "FLY_FAIL_NODES": "7",
    "FLY_DOWNSTREAM_SLEEP": "2.0",
})
print(r1.stderr, file=sys.stderr)
if r1.returncode != 0:
    print(f"Run 1 FAILED (exit={r1.returncode})", file=sys.stderr)
    print(r1.stdout, file=sys.stderr)
    sys.exit(1)
assert os.path.isfile(os.path.join(DB_PATH, "_test_db_id")), "marker should exist"
print("  Run 1 passed", file=sys.stderr)

# ── Run 2: load_db + restart → 全部成功 ──
print("── Run 2: load_db + restart_failed_tasks ──", file=sys.stderr)
r2 = run_script("chain_fail_run2.py", SHARED_LOG)
print(r2.stderr, file=sys.stderr)
if r2.returncode != 0:
    print(f"Run 2 FAILED (exit={r2.returncode})", file=sys.stderr)
    print(r2.stdout, file=sys.stderr)
    sys.exit(1)
print("  Run 2 passed", file=sys.stderr)

print("[PASS] test_chain_failure_restart: chain failure + cross-process restart verified",
      file=sys.stderr)
