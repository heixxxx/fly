"""E2E test: Pending tasks persisted on shutdown and recoverable.

Uses multi-process coordinator pattern:
  - Run 1: Submit tasks, some complete, some fail (unresolvable deps),
           stop master, verify failed_tasks.bin exists.
  - Run 2: New master with same log_dir, call restart_failed_tasks(),
           verify tasks are re-submitted and either complete or remain pending.

Run as coordinator: spawns Run 1 and Run 2 as subprocess via fly binary.
"""
import time
import sys
import os
import shutil
import subprocess

DB_PATH = "/tmp/fly_e2e_pending_persist_db"
LOG_DIR = "/tmp/fly_e2e_pending_persist_logs"

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(SCRIPT_DIR)
FLY_BIN = os.path.join(PROJECT_ROOT, "build", "bin", "fly")

sys.path.insert(0, os.path.join(SCRIPT_DIR, '..', 'src'))


def cleanup():
    for path in [DB_PATH, LOG_DIR]:
        if os.path.isdir(path):
            shutil.rmtree(path, ignore_errors=True)


def run_script(script_name, log_dir):
    script_path = os.path.join(SCRIPT_DIR, script_name)
    result = subprocess.run(
        [FLY_BIN, "--log-dir", log_dir, script_path],
        capture_output=True, text=True, timeout=120, cwd=PROJECT_ROOT,
    )
    if result.returncode != 0:
        print(f"  {script_name} FAILED:", file=sys.stderr)
        print(result.stderr[-2000:] if result.stderr else "(no stderr)",
              file=sys.stderr)
        print(result.stdout[-2000:] if result.stdout else "(no stdout)",
              file=sys.stderr)
    return result


def test_pending_task_persist():
    cleanup()

    # -- Phase 1: Run script that creates failed tasks and stops --
    result1 = run_script("pending_persist_run1.py", LOG_DIR)
    assert result1.returncode == 0, \
        f"Run 1 failed with exit code {result1.returncode}"
    print("  Phase 1 OK: Run 1 completed", file=sys.stderr)

    # -- Phase 2: Verify failed_tasks.bin was created --
    failed_file = os.path.join(LOG_DIR, "failed_tasks.bin")
    assert os.path.isfile(failed_file), \
        f"Phase 2: failed_tasks.bin should exist at {failed_file}"
    file_size = os.path.getsize(failed_file)
    assert file_size > 0, \
        "Phase 2: failed_tasks.bin should not be empty"
    print(f"  Phase 2 OK: failed_tasks.bin exists ({file_size} bytes)",
          file=sys.stderr)

    # -- Phase 3: Run script that restarts failed tasks --
    result2 = run_script("pending_persist_run2.py", LOG_DIR)
    assert result2.returncode == 0, \
        f"Run 2 failed with exit code {result2.returncode}"
    print("  Phase 3 OK: Run 2 completed", file=sys.stderr)

    print("[PASS] test_pending_task_persist", file=sys.stderr)


test_pending_task_persist()
