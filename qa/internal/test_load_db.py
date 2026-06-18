"""E2E test: load_db two-process lifecycle.

Phase 1: _DB_META incremental format (standalone, no Master/Worker)
Phase 2: Two-process normal case — same path for Run 1 and Run 2
Phase 3: Two-process moved-DB case — DB moved to different path between runs

Each run executes in a separate process via the fly binary, ensuring
fresh C++ singletons and true process-restart semantics.
"""
import os
import sys
import subprocess
import shutil
import time

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(os.path.dirname(SCRIPT_DIR))
FLY_BIN = os.path.join(PROJECT_ROOT, "bazel-bin", "src", "main", "cpp", "fly")

DB_PATH_P2 = "/tmp/fly_e2e_load_db_twoproc"
DB_PATH_P3_RUN1 = "/tmp/fly_e2e_load_db_moved_run1"
DB_PATH_P3_RUN2 = "/tmp/fly_e2e_load_db_moved_run2"


def cleanup(path):
    if os.path.isdir(path):
        shutil.rmtree(path, ignore_errors=True)


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


# ── Phase 1: Standalone _DB_META format test (no subprocess) ──

def test_db_meta_incremental_format():
    """Verify _DB_META incremental format via direct C++ API."""
    db_path = "/tmp/fly_e2e_load_db_p1"
    log_dir = "/tmp/fly_e2e_load_db_p1_logs"

    for p in [db_path, log_dir]:
        cleanup(p)

    import _fly_log as log
    import _fly_storage as storage

    log.init_log(log_dir, 0)

    try:
        sm = storage.ex_stg_get_storage_manager()
        ds = storage.ex_stg_get_data_service()

        db = sm.get_or_create_database(db_path)
        meta_path = os.path.join(db_path, "_DB_META")
        assert os.path.isfile(meta_path), "_DB_META should exist after construction"

        db.write_object_raw("obj/a", "data_a")
        db.write_object_raw("obj/b", "data_b")
        ds.drain_write_back()
        time.sleep(0.3)

        assert db.read_object_raw("obj/a") == "data_a"
        assert db.read_object_raw("obj/b") == "data_b"

        meta = db.load_meta()
        assert meta is not None
        assert meta.db_id == db.get_db_id()
        assert meta.created_at > 0
        assert len(meta.workers) == 0

        db.freeze()
        assert db.is_frozen()
        assert os.path.isfile(os.path.join(db_path, "_FROZEN"))

        sm.close_all()
        print("[PASS] test_db_meta_incremental_format", file=sys.stderr)

    finally:
        log.shutdown_log()
        for p in [db_path, log_dir]:
            cleanup(p)


# ── Phase 2: Two-process normal case (same path) ──

def test_load_db_two_processes():
    """Two-process load_db lifecycle: Run 1 creates, Run 2 loads and reads."""
    cleanup(DB_PATH_P2)

    log_dir = os.path.join(SCRIPT_DIR, "logs", "load_db_twoproc")

    # ── Run 1: Create DB, write data, no freeze ──
    print("── Phase 2 Run 1: Creating DB and writing data ──", file=sys.stderr)
    r1 = run_script("load_db_run1.py", os.path.join(log_dir, "run1"))

    print(r1.stderr, file=sys.stderr)
    if r1.returncode != 0:
        print(f"Run 1 FAILED (exit={r1.returncode})", file=sys.stderr)
        print(r1.stdout, file=sys.stderr)
        assert False, f"Run 1 failed with exit code {r1.returncode}\n{r1.stderr}"

    # Verify DB artifacts exist after Run 1
    assert os.path.isdir(DB_PATH_P2), "DB directory should exist after Run 1"
    assert os.path.isfile(os.path.join(DB_PATH_P2, "_DB_META")), "_DB_META should exist"
    assert os.path.isfile(os.path.join(DB_PATH_P2, "_test_db_id")), "Marker file should exist"
    assert not os.path.isfile(os.path.join(DB_PATH_P2, "_FROZEN")), \
        "Should NOT be frozen after Run 1"

    print("  Run 1 passed", file=sys.stderr)

    # ── Run 2: load_db, read data, new tasks, freeze ──
    print("── Phase 2 Run 2: Loading DB, reading, new tasks, freezing ──", file=sys.stderr)
    r2 = run_script("load_db_run2.py", os.path.join(log_dir, "run2"))

    print(r2.stderr, file=sys.stderr)
    if r2.returncode != 0:
        print(f"Run 2 FAILED (exit={r2.returncode})", file=sys.stderr)
        print(r2.stdout, file=sys.stderr)
        assert False, f"Run 2 failed with exit code {r2.returncode}\n{r2.stderr}"

    # Verify frozen after Run 2
    assert os.path.isfile(os.path.join(DB_PATH_P2, "_FROZEN")), \
        "Should be frozen after Run 2"

    print("  Run 2 passed", file=sys.stderr)
    print("[PASS] test_load_db_two_processes: two-process lifecycle verified",
          file=sys.stderr)


# ── Phase 3: Two-process moved-DB case ──

def test_load_db_moved_db():
    """Two-process moved-DB lifecycle: Run 1 at path A, move to B, Run 2 loads from B."""
    cleanup(DB_PATH_P3_RUN1)
    cleanup(DB_PATH_P3_RUN2)

    log_dir = os.path.join(SCRIPT_DIR, "logs", "load_db_moved")

    # ── Run 1: Create DB at path A ──
    print("── Phase 3 Run 1: Creating DB at path A ──", file=sys.stderr)
    r1 = run_script("load_db_run1_moved.py", os.path.join(log_dir, "run1"))

    print(r1.stderr, file=sys.stderr)
    if r1.returncode != 0:
        print(f"Run 1 FAILED (exit={r1.returncode})", file=sys.stderr)
        print(r1.stdout, file=sys.stderr)
        assert False, f"Run 1 failed with exit code {r1.returncode}\n{r1.stderr}"

    assert os.path.isdir(DB_PATH_P3_RUN1), "DB should exist at path A"
    assert os.path.isfile(os.path.join(DB_PATH_P3_RUN1, "_DB_META")), "_DB_META should exist"

    print("  Run 1 passed", file=sys.stderr)

    # ── Move DB directory from path A to path B ──
    print(f"── Phase 3: Moving DB from {DB_PATH_P3_RUN1} to {DB_PATH_P3_RUN2} ──",
          file=sys.stderr)
    shutil.move(DB_PATH_P3_RUN1, DB_PATH_P3_RUN2)

    assert os.path.isdir(DB_PATH_P3_RUN2), "DB should exist at path B after move"
    assert not os.path.isdir(DB_PATH_P3_RUN1), "DB should NOT exist at path A after move"

    print("  Move successful", file=sys.stderr)

    # ── Run 2: load_db from path B (different from original path A) ──
    print("── Phase 3 Run 2: Loading DB from moved path B ──", file=sys.stderr)
    r2 = run_script("load_db_run2_moved.py", os.path.join(log_dir, "run2"))

    print(r2.stderr, file=sys.stderr)
    if r2.returncode != 0:
        print(f"Run 2 FAILED (exit={r2.returncode})", file=sys.stderr)
        print(r2.stdout, file=sys.stderr)
        assert False, f"Run 2 failed with exit code {r2.returncode}\n{r2.stderr}"

    assert os.path.isfile(os.path.join(DB_PATH_P3_RUN2, "_FROZEN")), \
        "Should be frozen after Run 2"

    print("  Run 2 passed", file=sys.stderr)
    print("[PASS] test_load_db_moved_db: moved-DB lifecycle verified", file=sys.stderr)


test_db_meta_incremental_format()
print()
test_load_db_two_processes()
print()
test_load_db_moved_db()
print("\nAll load_db E2E tests passed!")
