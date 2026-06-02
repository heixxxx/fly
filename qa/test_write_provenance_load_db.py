"""E2E test: write provenance - load_db then rerun failed task is idempotent.

Run 1: write data, verify. Exit.
Run 2: load_db, submit same write_data(db, key, 42) → same hash from @as_task
  → Worker local pre-check: new hash matches IndexEntry hash → accept
  → Master: no stored provenance (fresh process) → accept
"""
import time
import sys
import os
import subprocess
import shutil

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', 'src'))

PROJECT_ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
FLY_BIN = os.path.join(PROJECT_ROOT, "bazel-bin", "src", "main", "cpp", "fly")

DB_PATH = "/tmp/fly_e2e_provenance_load_db_db"
LOG_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                       "logs", "provenance_load_db")


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)
    if os.path.isdir(LOG_DIR):
        shutil.rmtree(LOG_DIR, ignore_errors=True)


def run_script(script_name, log_dir):
    script_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), script_name)
    os.makedirs(log_dir, exist_ok=True)
    result = subprocess.run(
        [FLY_BIN, "--log-dir", log_dir, script_path],
        capture_output=True, text=True, timeout=120, cwd=PROJECT_ROOT,
    )
    if result.returncode != 0:
        print(f"FAILED: {script_name}", file=sys.stderr)
        print(f"stdout: {result.stdout}", file=sys.stderr)
        print(f"stderr: {result.stderr}", file=sys.stderr)
    assert result.returncode == 0, f"{script_name} failed (exit {result.returncode})"
    return result


def test_write_provenance_load_db():
    cleanup()

    run1_log = os.path.join(LOG_DIR, "run1")
    run2_log = os.path.join(LOG_DIR, "run2")

    run_script("provenance_load_db_run1.py", run1_log)
    run_script("provenance_load_db_run2.py", run2_log)

    print("[PASS] test_write_provenance_load_db: load_db + rerun accepted", file=sys.stderr)


test_write_provenance_load_db()
print("\nAll tests passed!")
