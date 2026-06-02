import os
import sys
import subprocess
import shutil

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(SCRIPT_DIR)
FLY_BIN = os.path.join(PROJECT_ROOT, "bazel-bin", "src", "main", "cpp", "fly")

DB_PATH = "/tmp/fly_e2e_save_to_db_false_persist_db"


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


def run_script(script_name, log_dir, timeout=120):
    script_path = os.path.join(SCRIPT_DIR, script_name)
    os.makedirs(log_dir, exist_ok=True)
    result = subprocess.run(
        [FLY_BIN, "--log-dir", log_dir, script_path],
        capture_output=True, text=True, timeout=timeout, cwd=PROJECT_ROOT,
    )
    return result


def test_temp_not_persisted_across_restart():
    cleanup()

    log_dir = os.path.join(SCRIPT_DIR, "logs", "save_to_db_false")

    print("-- Run 1: write persistent + temp data --", file=sys.stderr)
    r1 = run_script("save_to_db_false_run1.py", os.path.join(log_dir, "run1"))
    print(r1.stderr, file=sys.stderr)
    assert r1.returncode == 0, f"Run 1 failed: {r1.stderr}"

    assert os.path.isdir(DB_PATH), "DB should exist after Run 1"

    print("-- Run 2: verify temp data gone, persistent data survives --", file=sys.stderr)
    r2 = run_script("save_to_db_false_run2.py", os.path.join(log_dir, "run2"))
    print(r2.stderr, file=sys.stderr)
    assert r2.returncode == 0, f"Run 2 failed: {r2.stderr}"

    print("[PASS] test_save_to_db_false_persist: temp data not persisted across restart",
          file=sys.stderr)


test_temp_not_persisted_across_restart()
