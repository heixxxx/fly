"""E2E test: backup data on two virtual hosts, load_db with two workers, distributed read.

Uses --host override to simulate multi-host scenario on a single machine.
Run 1: Two workers with different --host values write+backup data.
Run 2: load_db with workers on different virtual hosts, verify cross-host reads.
"""
from _fly_log import INFO
import os
import subprocess
import shutil
import time

from fly import get_fly_binary

DB_PATH = "/tmp/fly_e2e_backup_load_db_multi_worker"
PROJECT_ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
QA_DIR = os.path.dirname(os.path.abspath(__file__))
FLY_BIN = get_fly_binary()

LOG_DIR = os.path.join(QA_DIR, "logs", "test_backup_load_db_multi_worker")


def run_script(script_name, log_subdir, extra_args=None, timeout=120):
    """Run a fly script as a subprocess and check exit code."""
    script_path = os.path.join(QA_DIR, script_name)
    log_dir = os.path.join(LOG_DIR, log_subdir)
    os.makedirs(log_dir, exist_ok=True)

    cmd = [FLY_BIN, "--log-dir", log_dir]
    if extra_args:
        cmd.extend(extra_args)
    cmd.append(script_path)

    result = subprocess.run(
        cmd, capture_output=True, text=True, timeout=timeout, cwd=PROJECT_ROOT,
    )
    if result.returncode != 0:
        INFO(f"[FAIL] Script {script_name} exited with code {result.returncode}")
        INFO(f"  stdout: {result.stdout[-2000:]}")
        INFO(f"  stderr: {result.stderr[-2000:]}")
    return result


# ── Cleanup ──
if os.path.isdir(DB_PATH):
    shutil.rmtree(DB_PATH, ignore_errors=True)
os.makedirs(DB_PATH, exist_ok=True)

# ── Execute Run 1 ──
# Run 1 master is on host-alpha, so --host host-alpha
INFO("[TEST] Running Run 1: write+backup on two virtual hosts")
result1 = run_script("backup_load_db_multi_worker_run1.py", "run1",
                     extra_args=["--host", "host-alpha"])
assert result1.returncode == 0, f"Run 1 failed with exit code {result1.returncode}"
INFO("[TEST] Run 1 completed successfully")

# Small delay between runs
time.sleep(1.0)

# ── Execute Run 2 ──
# Run 2 master is also on host-alpha
INFO("[TEST] Running Run 2: load_db + distributed reads")
result2 = run_script("backup_load_db_multi_worker_run2.py", "run2",
                     extra_args=["--host", "host-alpha"])
assert result2.returncode == 0, f"Run 2 failed with exit code {result2.returncode}"
INFO("[TEST] Run 2 completed successfully")

INFO("[PASS] test_backup_load_db_multi_worker")
