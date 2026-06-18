"""E2E test: @wait_obj decorator — block until data ready, then execute locally.

Uses subprocess-per-phase pattern (same as test_load_db.py).
Each phase runs as a separate fly binary process, ensuring
fresh C++ singletons and no stale state between runs.

Phases:
  1. Master writes → @wait_obj immediate → read + verify
  2. Worker writes → @wait_obj blocks → read + verify
  3. Multiple deps → @wait_obj waits for all → execute + verify
  4. Timeout → @wait_obj raises TimeoutError on phantom data
  5. Worker task internally uses @wait_obj to wait for upstream data
"""
from _fly_log import INFO
import os
import sys
import subprocess
import shutil

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(os.path.dirname(SCRIPT_DIR))
FLY_BIN = os.path.join(PROJECT_ROOT, "bazel-bin", "src", "main", "cpp", "fly")

HELPERS = [
    "wait_obj_p1_master.py",
    "wait_obj_p2_worker.py",
    "wait_obj_p3_multi.py",
    "wait_obj_p4_timeout.py",
    "wait_obj_p5_inside_task.py",
]
PHASE_NAMES = [
    "Master write → immediate read",
    "Worker write → block & read",
    "Multi deps → wait for all",
    "Timeout → phantom data",
    "Cross-worker @wait_obj (2 workers)",
]


def run_script(script_name, log_dir):
    """Run a Python script via the fly binary in a subprocess."""
    script_path = os.path.join(SCRIPT_DIR, script_name)
    os.makedirs(log_dir, exist_ok=True)

    result = subprocess.run(
        [FLY_BIN, "--log-dir", log_dir, script_path],
        capture_output=True,
        text=True,
        timeout=120,
        cwd=PROJECT_ROOT,
    )
    return result


def test_all_phases():
    log_base = os.path.join(SCRIPT_DIR, "logs", "wait_obj")

    for name, helper in zip(PHASE_NAMES, HELPERS):
        log_dir = os.path.join(log_base, helper.replace(".py", ""))
        INFO(f"── {name}: {helper} ──")

        r = run_script(helper, log_dir)
        INFO(r.stderr)

        if r.returncode != 0:
            INFO(f"FAILED (exit={r.returncode})")
            INFO(r.stdout)
            assert False, f"Phase '{name}' failed\n{r.stderr}"

        INFO("")


test_all_phases()
INFO("\nAll wait_obj E2E tests passed!")
