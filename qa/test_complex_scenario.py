"""Complex scenario: multi-DB, cross-DB deps, load_db migration, dynamic properties, restart."""
import subprocess
import sys
import os
import shutil

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(SCRIPT_DIR)
FLY_BIN = os.path.join(PROJECT_ROOT, "bazel-bin", "src", "main", "cpp", "fly")

DB_RAW = "/tmp/fly_complex_db_raw"
DB_FEAT = "/tmp/fly_complex_db_feat"
DB_RAW_MOVED = "/tmp/fly_complex_db_raw_moved"
DB_FEAT_MOVED = "/tmp/fly_complex_db_feat_moved"
DB_MODEL = "/tmp/fly_complex_db_model"


def cleanup():
    for p in [DB_RAW, DB_FEAT, DB_RAW_MOVED, DB_FEAT_MOVED, DB_MODEL]:
        if os.path.isdir(p):
            shutil.rmtree(p, ignore_errors=True)


def run_script(script_name, log_dir, timeout=120):
    script_path = os.path.join(SCRIPT_DIR, script_name)
    result = subprocess.run(
        [FLY_BIN, "--log-dir", log_dir, script_path],
        capture_output=True, text=True, timeout=timeout, cwd=PROJECT_ROOT,
    )
    return result


cleanup()

print("=== Run 1: Initial data production ===", file=sys.stderr)
r1 = run_script("complex_run1.py", "/tmp/fly_complex_logs_run1")
print(r1.stderr, file=sys.stderr)
assert r1.returncode == 0, f"Run 1 failed:\n{r1.stderr}"
print("Run 1 PASSED\n", file=sys.stderr)

print("=== Run 2: load_db + migration + dynamic props + restart ===", file=sys.stderr)
r2 = run_script("complex_run2.py", "/tmp/fly_complex_logs_run2")
print(r2.stderr, file=sys.stderr)
assert r2.returncode == 0, f"Run 2 failed:\n{r2.stderr}"
print("Run 2 PASSED\n", file=sys.stderr)

print("[PASS] test_complex_scenario — all features verified across 2 runs", file=sys.stderr)
