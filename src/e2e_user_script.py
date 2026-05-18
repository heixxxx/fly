import subprocess
import sys
import os

TESTS = [
    "test_worker_db_write.py",
    "test_dependency_and_freeze.py",
    "test_read_frozen_db.py",
    "test_write_frozen_db_fails.py",
    "test_recursive_submit.py",
    "test_concurrent_read.py",
]

TEST_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.abspath(os.path.join(TEST_DIR, '..', '..'))
FLY_BIN = os.path.join(PROJECT_ROOT, 'bazel-bin', 'src', 'main', 'cpp', 'fly')


def run_all():
    for test_file in TESTS:
        test_path = os.path.join(TEST_DIR, test_file)
        test_name = test_file.replace(".py", "")
        print(f"\n=== {test_name} ===", file=sys.stderr)

        result = subprocess.run(
            [FLY_BIN, test_path],
            timeout=120
        )

        if result.returncode != 0:
            print(f"[FAIL] {test_name}", file=sys.stderr)
            sys.exit(1)

        print(f"[PASS] {test_name}", file=sys.stderr)

    print("\n=== All E2E tests passed ===", file=sys.stderr)


if __name__ == "__main__":
    run_all()
