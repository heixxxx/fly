"""Complex scenario: multi-DB, cross-DB deps, load_db migration, dynamic properties, restart."""
from _fly_log import INFO
import subprocess
import os
import shutil

from fly import get_fly_binary, get_config

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(os.path.dirname(SCRIPT_DIR))
FLY_BIN = get_fly_binary()

_DB_DIR = get_config().get_str("log_dir")
DB_RAW = os.path.join(_DB_DIR, "db_raw")
DB_FEAT = os.path.join(_DB_DIR, "db_feat")
DB_RAW_MOVED = os.path.join(_DB_DIR, "db_raw_moved")
DB_FEAT_MOVED = os.path.join(_DB_DIR, "db_feat_moved")
DB_MODEL = os.path.join(_DB_DIR, "db_model")


def cleanup():
    for p in [DB_RAW, DB_FEAT, DB_RAW_MOVED, DB_FEAT_MOVED, DB_MODEL]:
        if os.path.isdir(p):
            shutil.rmtree(p, ignore_errors=True)


def _merge_env(extra):
    """Merge extra env vars into a copy of os.environ (safe for parallel execution)."""
    e = os.environ.copy()
    if extra:
        e.update(extra)
    return e

def run_script(script_name, log_dir, timeout=120, extra_env=None):
    script_path = os.path.join(SCRIPT_DIR, script_name)
    result = subprocess.run(
        [FLY_BIN, "--log-dir", log_dir, script_path],
        capture_output=True, text=True, timeout=timeout, cwd=PROJECT_ROOT,
        env=_merge_env(extra_env),
    )
    return result


cleanup()

INFO("=== Run 1: Initial data production ===")
r1 = run_script("complex_run1.py", os.path.join(_DB_DIR, "logs_run1"), extra_env={"FLY_DB_DIR": _DB_DIR})
INFO(r1.stderr)
assert r1.returncode == 0, f"Run 1 failed:\n{r1.stderr}"
INFO("Run 1 PASSED\n")

INFO("=== Run 2: load_db + migration + dynamic props + restart ===")
r2 = run_script("complex_run2.py", os.path.join(_DB_DIR, "logs_run2"), extra_env={"FLY_DB_DIR": _DB_DIR})
INFO(r2.stderr)
assert r2.returncode == 0, f"Run 2 failed:\n{r2.stderr}"
INFO("Run 2 PASSED\n")

INFO("[PASS] test_complex_scenario — all features verified across 2 runs")
