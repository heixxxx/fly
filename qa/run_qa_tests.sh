#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

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
    TEST_FILES=$(find "$SCRIPT_DIR" -maxdepth 1 -name "test_*.py" -type f | sort)
fi

LOG_BASE="$SCRIPT_DIR/logs"
rm -rf "$LOG_BASE"
mkdir -p "$LOG_BASE"

PASSED=0
FAILED=0
FAILED_TESTS=()

echo "=========================================="
echo "Fly QA Test Suite"
echo "=========================================="
echo "Fly binary: $FLY_BIN"
echo "Test directory: $SCRIPT_DIR"
echo "Tests to run: ${#TEST_FILES[@]}"
echo "=========================================="
echo

for test_file in ${TEST_FILES[@]}; do
    test_name=$(basename "$test_file")
    LOG_DIR="$LOG_BASE/${test_name%.py}"
    "$FLY_BIN" --log-dir "$LOG_DIR" "$test_file" > "$LOG_BASE/${test_name%.py}_output.log" 2>&1 &
    echo "$! $test_file" >> /tmp/qa_pids_$$
done

echo
echo "Waiting for ${#TEST_FILES[@]} tests to complete..."
echo

while IFS=' ' read -r pid test_file; do
    test_name=$(basename "$test_file")
    if wait "$pid"; then
        echo "  ✓ PASSED  ${test_name}"
        PASSED=$((PASSED + 1))
    else
        echo "  ✗ FAILED  ${test_name}"
        FAILED=$((FAILED + 1))
        FAILED_TESTS+=("$test_name")
        echo "  Last 20 lines of output:"
        tail -20 "$LOG_BASE/${test_name%.py}_output.log" | sed 's/^/    /'
    fi
done < /tmp/qa_pids_$$
rm -f /tmp/qa_pids_$$

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
