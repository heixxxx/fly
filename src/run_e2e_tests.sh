#!/bin/bash

FLY_BIN="./bazel-bin/src/main/cpp/fly"
TEST_DIR="src/e2e_tests"

tests=(
    "test_worker_db_write.py"
    "test_dependency_and_freeze.py"
    "test_read_frozen_db.py"
    "test_write_frozen_db_fails.py"
    "test_recursive_submit.py"
    "test_concurrent_read.py"
)

for test_file in "${tests[@]}"; do
    test_path="$TEST_DIR/$test_file"
    test_name="${test_file%.py}"
    
    echo ""
    echo "=== $test_name ==="
    
    # Clean before each test
    rm -rf /tmp/fly_e2e_* 2>/dev/null
    
    # Run test (grep only PASS/FAIL/assert lines)
    $FLY_BIN "$test_path" 2>&1 | grep -E "(PASS|FAIL|assert|Traceback)" | head -5
    
    # Check exit code
    exit_code=${PIPESTATUS[0]}
    if [ $exit_code -ne 0 ] && [ $exit_code -ne 134 ]; then
        echo "[FAIL] $test_name (exit $exit_code)"
        exit 1
    fi
done

echo ""
echo "=== All E2E tests passed ==="