#!/usr/bin/env bash
# measure_coverage.sh — Measure Python + C++ code coverage for Fly
#
# Usage:
#   ./tools/measure_coverage.sh python          # Python coverage (Master + Worker)
#   ./tools/measure_coverage.sh cpp             # C++ coverage (unit tests + QA)
#   ./tools/measure_coverage.sh all             # Both
#
# Output:
#   /tmp/fly_coverage/python/  — HTML + JSON + text report
#   /tmp/fly_coverage/cpp/     — HTML + lcov report

set -euo pipefail

FLY_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUTPUT_DIR="/tmp/fly_coverage"
FLY_BIN="$FLY_ROOT/build/bin/fly"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

info()  { echo -e "${GREEN}[INFO]${NC} $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC} $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*"; exit 1; }

# ─── Python Coverage ───────────────────────────────────────────────
measure_python() {
    info "Measuring Python coverage (Master + Worker processes)..."

    local py_dir="$OUTPUT_DIR/python"
    rm -rf "$py_dir" /tmp/.coverage* "$FLY_ROOT/.coverage"*

    # Build and install
    "$FLY_ROOT/fly.sh" build //src/main/cpp:fly
    "$FLY_ROOT/fly.sh" install

    # Run ALL QA tests with FLY_PYCOVERAGE=1 (tracks all Python modules)
    FLY_PYCOVERAGE=1 bash "$FLY_ROOT/qa/run_qa_tests.sh"

    # Copy Master data files (one per QA test process, named by PID)
    for f in /tmp/.coverage.fly.master.*; do
        [ -f "$f" ] || continue
        local pid
        pid=$(basename "$f" | sed 's/.coverage.fly.master.//')
        cp "$f" "$FLY_ROOT/.coverage.master_$pid"
    done

    # Copy Worker data files alongside master for combining
    for f in /tmp/.coverage.fly.worker_*; do
        [ -f "$f" ] || continue
        local suffix
        suffix=$(basename "$f" | sed 's/.coverage.fly.//')
        cp "$f" "$FLY_ROOT/.coverage.$suffix"
    done

    # Combine all coverage data (masters + workers)
    cd "$FLY_ROOT"
    coverage combine 2>&1 || warn "coverage combine had issues"

    # Generate reports
    mkdir -p "$py_dir"
    coverage report --show-missing > "$py_dir/report.txt" 2>&1
    coverage json -o "$py_dir/coverage.json" 2>&1
    coverage html -d "$py_dir/html" 2>&1

    # Print summary
    echo ""
    info "Python Coverage Report (Master + Workers combined):"
    cat "$py_dir/report.txt"
    echo ""
    info "HTML report: $py_dir/html/index.html"
    info "JSON report: $py_dir/coverage.json"

    # Cleanup
    rm -f "$FLY_ROOT/.coverage"* /tmp/.coverage*
}

# ─── C++ Coverage ───────────────────────────────────────────────────
measure_cpp() {
    info "Measuring C++ coverage (unit tests + QA)..."

    local cpp_dir="$OUTPUT_DIR/cpp"
    rm -rf "$cpp_dir"
    mkdir -p "$cpp_dir"

    # Build with coverage instrumentation
    info "Building with --config=coverage..."
    bazel build --config=coverage //src/... 2>&1 | tail -3

    # Find Bazel output directory
    local BAZEL_BIN="$FLY_ROOT/bazel-bin"

    # Clean old .gcda files
    info "Cleaning old coverage data..."
    find "$BAZEL_BIN" -name "*.gcda" -delete 2>/dev/null || true

    # ── Run C++ unit tests ──
    info "Running C++ unit tests..."
    local test_count=0
    local test_pass=0
    for test_bin in $(find "$BAZEL_BIN/src" -type f -name "*_test" -executable 2>/dev/null); do
        test_count=$((test_count + 1))
        if timeout 60 "$test_bin" > /dev/null 2>&1; then
            test_pass=$((test_pass + 1))
        else
            warn "FAILED: $(basename $test_bin)"
        fi
    done
    info "C++ unit tests: $test_pass/$test_count passed"

    # ── Run QA integration tests ──
    info "Running QA integration tests with coverage..."
    "$FLY_ROOT/fly.sh" install
    export GCOV_PREFIX="$cpp_dir/gcov_workers"
    mkdir -p "$GCOV_PREFIX"

    bash "$FLY_ROOT/qa/run_qa_tests.sh" 2>&1 | tail -5

    # ── Collect coverage data ──
    info "Collecting C++ coverage data with lcov..."

    local OBJ_DIRS=""
    for dir in agent storage network task core log serialization common main; do
        local d="$BAZEL_BIN/src/$dir/cpp/_objs"
        if [ -d "$d" ]; then
            OBJ_DIRS="$OBJ_DIRS --directory $d"
        fi
    done

    lcov --capture $OBJ_DIRS \
        --output-file "$cpp_dir/coverage_raw.info" \
        --rc lcov_branch_coverage=1 \
        --gcov-tool /usr/bin/gcov-12 \
        --ignore-errors source 2>&1 | tail -3

    # Filter system headers and test files
    lcov --remove "$cpp_dir/coverage_raw.info" \
        '/usr/include/*' \
        '*/external/*' \
        '*/tests/*' \
        '*/test_*' \
        '*/export/*' \
        --rc lcov_branch_coverage=1 \
        --output-file "$cpp_dir/coverage.info" 2>&1 | tail -3

    # Fix source paths (Bazel uses /proc/self/cwd/ prefix)
    sed -i 's|/proc/self/cwd/|'"$FLY_ROOT"'/|g' "$cpp_dir/coverage.info"

    # Generate HTML report
    genhtml "$cpp_dir/coverage.info" \
        --branch-coverage --legend \
        --output-directory "$cpp_dir/html" 2>&1 | tail -5

    # Print summary
    echo ""
    info "C++ Coverage Summary:"
    lcov --summary "$cpp_dir/coverage.info" --rc lcov_branch_coverage=1 2>&1 | grep -E "(lines|functions|branches)"
    echo ""
    info "HTML report: $cpp_dir/html/index.html"
    info "lcov data: $cpp_dir/coverage.info"
}

# ─── Main ────────────────────────────────────────────────────────────
case "${1:-all}" in
    python|py)
        measure_python
        ;;
    cpp|c++)
        measure_cpp
        ;;
    all)
        measure_python
        echo ""
        measure_cpp
        ;;
    *)
        echo "Usage: $0 {python|cpp|all}"
        exit 1
        ;;
esac
