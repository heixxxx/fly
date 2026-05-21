#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
QA_DIR="$PROJECT_ROOT/qa"

FLY_BIN="$PROJECT_ROOT/bazel-bin/src/main/cpp/fly"
if [ ! -x "$FLY_BIN" ]; then
    echo "Building fly binary..."
    cd "$PROJECT_ROOT"
    ./fly.sh build //src/main/cpp:fly
    FLY_BIN="$PROJECT_ROOT/bazel-bin/src/main/cpp/fly"
fi

if [ -n "$1" ]; then
    TEST_FILES=("$1")
else
    TEST_FILES=$(find "$QA_DIR" -name "test_*.py" -type f | sort)
fi

PASSED=0
FAILED=0
FAILED_TESTS=()

echo "=========================================="
echo "Fly QA Test Suite"
echo "=========================================="
echo "Fly binary: $FLY_BIN"
echo "Test directory: $QA_DIR"
echo "Tests to run: ${#TEST_FILES[@]}"
echo "=========================================="
echo

for test_file in ${TEST_FILES[@]}; do
    test_name=$(basename "$test_file")
    echo "Running: $test_name"
    
    LOG_DIR="$PROJECT_ROOT/qa_logs/${test_name%.py}"
    mkdir -p "$LOG_DIR"
    
    cd "$PROJECT_ROOT"
    if $FLY_BIN --log-dir "$LOG_DIR" "$test_file" > "$LOG_DIR/output.log" 2>&1; then
        echo "  ✓ PASSED"
        PASSED=$((PASSED + 1))
    else
        echo "  ✗ FAILED"
        FAILED=$((FAILED + 1))
        FAILED_TESTS+=("$test_name")
        echo "  Last 20 lines of output:"
        tail -20 "$LOG_DIR/output.log" | sed 's/^/    /'
    fi
    
    rm -rf "$LOG_DIR"
    echo
done

echo "=========================================="
echo "Summary"
echo "=========================================="
echo "Passed: $PASSED"
echo "Failed: $FAILED"

if [ $FAILED -gt 0 ]; then
    echo
    echo "Failed tests:"
    for t in ${FAILED_TESTS[@]}; do
        echo "  - $t"
    done
    exit 1
fi

echo
echo "All QA tests passed!"
exit 0