"""E2E test: remove_object — delete index, make object unreadable.

Uses subprocess-per-phase pattern (same as test_load_db.py).
Each phase runs as a separate fly binary process, ensuring
fresh C++ singletons and no stale state between runs.

Phase 1: Write + remove on same Worker, verify read fails
Phase 2: Write data, remove it, dependent task on removed data should fail
Phase 3: Write two objects, remove one, verify other still readable
"""
from _fly_log import INFO
import os
import sys
import subprocess
import shutil

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(os.path.dirname(SCRIPT_DIR))
FLY_BIN = os.path.join(PROJECT_ROOT, "bazel-bin", "src", "main", "cpp", "fly")


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


# ── Phase 1: write_and_remove basic ──

def test_remove_object_basic():
    INFO("── Phase 1: write_and_remove basic ──")
    log_dir = os.path.join(SCRIPT_DIR, "logs", "remove_obj", "phase1")

    r = run_script("remove_obj_phase1.py", log_dir)

    INFO(r.stderr)
    if r.returncode != 0:
        INFO(f"Phase 1 FAILED (exit={r.returncode})")
        INFO(r.stdout)
        assert False, f"Phase 1 failed with exit code {r.returncode}\n{r.stderr}"

    INFO("[PASS] test_remove_object_basic")


# ── Phase 2: dependent task fails after object removed ──

def test_remove_then_dependent_task_fails():
    INFO("── Phase 2: dependent task fails after remove ──")
    log_dir = os.path.join(SCRIPT_DIR, "logs", "remove_obj", "phase2")

    r = run_script("remove_obj_phase2.py", log_dir)

    INFO(r.stderr)
    if r.returncode != 0:
        INFO(f"Phase 2 FAILED (exit={r.returncode})")
        INFO(r.stdout)
        assert False, f"Phase 2 failed with exit code {r.returncode}\n{r.stderr}"

    INFO("[PASS] test_remove_then_dependent_task_fails")


# ── Phase 3: remove one, keep the other ──

def test_remove_one_keeps_other():
    INFO("── Phase 3: remove one, keep other readable ──")
    log_dir = os.path.join(SCRIPT_DIR, "logs", "remove_obj", "phase3")

    r = run_script("remove_obj_phase3.py", log_dir)

    INFO(r.stderr)
    if r.returncode != 0:
        INFO(f"Phase 3 FAILED (exit={r.returncode})")
        INFO(r.stdout)
        assert False, f"Phase 3 failed with exit code {r.returncode}\n{r.stderr}"

    INFO("[PASS] test_remove_one_keeps_other")


test_remove_object_basic()
INFO("")
test_remove_then_dependent_task_fails()
INFO("")
test_remove_one_keeps_other()
INFO("\nAll remove_object E2E tests passed!")
