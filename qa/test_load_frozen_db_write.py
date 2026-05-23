"""E2E test: load_db preserves frozen state across process restarts.

Phase 1 (frozen_db_run1.py): Create DB, write data via tasks, freeze.
Phase 2 (frozen_db_run2.py): load_db, verify is_frozen(), verify write_object fails.

Each phase runs in a separate fly process to ensure true process-restart semantics.
"""
import os
import sys
import subprocess
import shutil

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(SCRIPT_DIR)
FLY_BIN = os.path.join(PROJECT_ROOT, "bazel-bin", "src", "main", "cpp", "fly")

DB_PATH = "/tmp/fly_e2e_frozen_load_db"


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


def run_script(script_name, log_dir, timeout=120):
    """Run a Python script via the fly binary in a subprocess."""
    script_path = os.path.join(SCRIPT_DIR, script_name)
    os.makedirs(log_dir, exist_ok=True)
    result = subprocess.run(
        [FLY_BIN, "--log-dir", log_dir, script_path],
        capture_output=True, text=True, timeout=timeout, cwd=PROJECT_ROOT,
    )
    return result


def test_load_frozen_db_write():
    cleanup()

    log_dir = os.path.join(SCRIPT_DIR, "logs", "frozen_db")

    # ── Run 1: Create DB, write data, freeze ──
    print("── Run 1: Creating DB, writing data, freezing ──", file=sys.stderr)
    r1 = run_script("frozen_db_run1.py", os.path.join(log_dir, "run1"))
    print(r1.stderr, file=sys.stderr)
    if r1.returncode != 0:
        print(f"Run 1 FAILED (exit={r1.returncode})", file=sys.stderr)
        print(r1.stdout, file=sys.stderr)
        assert False, f"Run 1 failed with exit code {r1.returncode}\n{r1.stderr}"

    # Verify DB artifacts exist and is frozen after Run 1
    assert os.path.isdir(DB_PATH), "DB directory should exist after Run 1"
    assert os.path.isfile(os.path.join(DB_PATH, "_DB_META")), "_DB_META should exist"
    assert os.path.isfile(os.path.join(DB_PATH, "_FROZEN")), \
        "Should be frozen after Run 1"

    print("  Run 1 passed", file=sys.stderr)

    # ── Run 2: load_db, verify frozen, verify write fails ──
    print("── Run 2: Loading frozen DB, verifying frozen state ──", file=sys.stderr)
    r2 = run_script("frozen_db_run2.py", os.path.join(log_dir, "run2"))
    print(r2.stderr, file=sys.stderr)
    if r2.returncode != 0:
        print(f"Run 2 FAILED (exit={r2.returncode})", file=sys.stderr)
        print(r2.stdout, file=sys.stderr)
        assert False, f"Run 2 failed with exit code {r2.returncode}\n{r2.stderr}"

    print("  Run 2 passed", file=sys.stderr)
    print("[PASS] test_load_frozen_db_write: frozen state preserved across process restart",
          file=sys.stderr)


if __name__ == "__main__":
    test_load_frozen_db_write()
