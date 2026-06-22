import os
import shutil
import subprocess

from _fly_log import INFO
from fly import get_fly_binary, get_work_directory

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
FLY_BIN = get_fly_binary()
LOG_DIR = get_work_directory()
# Fixed DB path shared across run1/run2 subprocesses (SCRIPT_DIR-relative).
DB_PATH = os.path.join(SCRIPT_DIR, "persistence_db")


def main():
    # Clean any prior db.
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)

    env = dict(os.environ, FLY_VAR_PERSISTENCE_DB=DB_PATH)

    # Run 1: set vars, freeze (persists _VARS).
    result = subprocess.run(
        [FLY_BIN, "--log-dir", LOG_DIR, os.path.join(SCRIPT_DIR, "var_persistence_run1.py")],
        capture_output=True, text=True, timeout=120, cwd=SCRIPT_DIR, env=env,
    )
    assert result.returncode == 0, f"run1 failed: {result.stderr}"

    # Run 2: load_db, verify vars restored.
    result = subprocess.run(
        [FLY_BIN, "--log-dir", LOG_DIR, os.path.join(SCRIPT_DIR, "var_persistence_run2.py")],
        capture_output=True, text=True, timeout=120, cwd=SCRIPT_DIR, env=env,
    )
    assert result.returncode == 0, f"run2 failed: {result.stderr}"

    INFO(f"[PASS] test_var_persistence")


main()
