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
#
# Coverage is started at interpreter boot via sitecustomize.py (installed into
# build/python/ by fly.sh).  Setting FLY_PYCOVERAGE=1 is all that's needed —
# every fly process (master + workers) auto-instruments itself before any
# `import fly`, fixing the §12.1 flaw where fly/__init__.py imports ran before
# coverage.start().  Worker subprocesses inherit the env var and instrument
# themselves too.  See docs/coverage-testing.md §12.1.
measure_python() {
    info "Measuring Python coverage (Master + Worker processes)..."

    local py_dir="$OUTPUT_DIR/python"
    # .coveragerc pins data_file to PY_DATA_DIR (absolute path) so that runqa,
    # which runs each test with cwd = the test directory, doesn't scatter
    # parallel data files across qa/*/. Collect everything in one fixed place.
    local PY_DATA_DIR="/tmp/fly_py_coverage"
    rm -rf "$py_dir" "$PY_DATA_DIR"
    mkdir -p "$PY_DATA_DIR"

    # Build and install (install also lays down sitecustomize.py + .coveragerc)
    "$FLY_ROOT/fly.sh" build //src/main/cpp:fly
    "$FLY_ROOT/fly.sh" install

    # Run ALL QA tests with coverage auto-started by sitecustomize.
    # -j 1 is NOT required for Python (coverage.py's parallel mode gives each
    # process its own data file, no shared-write risk). We keep runqa's default
    # parallelism for speed.
    # -j 2：coverage tracer 显著放大每 fly 进程内存，-j6 曾把宿主 Windows commit
    # 打满（Resource-Exhaustion-Detector Event 2004 → WSL VM 整体重启，2026-08-16
    # 实测）。-j2 实测 guest 谷值 4.1GB、宿主安全。
    # 单 case 失败不阻断（覆盖率是诊断工具，已有数据仍需 combine 报告；
    # 失败明细在 runqa 输出与 qa/logs/qa.log 中，不掩盖）。
    FLY_PYCOVERAGE=1 bash "$FLY_ROOT/qa/run_qa_tests.sh" -j 2 || warn "QA had failures — combining coverage from passed cases anyway"

    # Combine all per-process data files from the pinned location.
    # parallel=True in .coveragerc makes each process write
    # $PY_DATA_DIR/.coverage.<host>.<pid>.<rand>; combine merges them into one.
    cd "$FLY_ROOT"
    coverage combine --data-file="$PY_DATA_DIR/.coverage" "$PY_DATA_DIR" 2>&1 \
        || warn "coverage combine had issues"

    # Generate reports
    mkdir -p "$py_dir"
    coverage report --data-file="$PY_DATA_DIR/.coverage" --show-missing > "$py_dir/report.txt" 2>&1
    coverage json --data-file="$PY_DATA_DIR/.coverage" -o "$py_dir/coverage.json" 2>&1
    coverage html --data-file="$PY_DATA_DIR/.coverage" -d "$py_dir/html" 2>&1

    # Print summary
    echo ""
    info "Python Coverage Report (Master + Workers combined):"
    cat "$py_dir/report.txt"
    echo ""
    info "HTML report: $py_dir/html/index.html"
    info "JSON report: $py_dir/coverage.json"

    # Cleanup
    rm -rf "$PY_DATA_DIR"
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

    # Clean old .gcda files.
    # NOTE: bazel-bin is a SYMLINK to /root/.cache/bazel/.../bazel-out/.../bin.
    # `find` does NOT follow symlinks by default, so a bare
    # `find "$BAZEL_BIN" -name "*.gcda" -delete` silently deletes nothing.
    # The leftover stale gcda (from a previous, possibly different, build)
    # then gets picked up by lcov --capture below, producing bogus 0% line
    # counts and "inconsistent" errors — exactly the symptom that made
    # data_client.cpp / decompress_helper.cpp look uncovered.  -L makes find
    # descend into the symlinked tree.
    info "Cleaning old coverage data..."
    find -L "$BAZEL_BIN" -name "*.gcda" -delete 2>/dev/null || true

    # ── Run C++ unit tests ──
    # timeout 300：master_agent_test（90 用例）等重量级测试单跑 ~90s，
    # timeout 60 会截断导致 agent 模块覆盖缺失（2026-08-16 实测修正）。
    # 只跑各 tests/BUILD 中仍注册的 target 名——bazel-bin 符号树里可能残留
    # 已删 target 的 stale 二进制（如 io_thread_pool_test），其 .so 已删必然失败。
    info "Running C++ unit tests..."
    local test_count=0
    local test_pass=0
    local live_targets
    live_targets=$(grep -h -A1 "^cc_test\|^py_test" $(find src -path "*/tests/BUILD") \
        | grep -oP 'name = "\K[^"]+' | sort -u)
    for test_bin in $(find "$BAZEL_BIN/src" -type f -name "*_test" -executable 2>/dev/null); do
        local base; base=$(basename "$test_bin")
        if ! echo "$live_targets" | grep -qx "$base"; then
            warn "SKIP stale binary: $base (target removed from BUILD)"
            continue
        fi
        test_count=$((test_count + 1))
        if timeout 300 "$test_bin" > /dev/null 2>&1; then
            test_pass=$((test_pass + 1))
        else
            warn "FAILED: $base"
        fi
    done
    info "C++ unit tests: $test_pass/$test_count passed"

    # ── Run QA integration tests ──
    #
    # gcda relocation — the real root cause of the §12.2 flaw.
    #
    # gcov records each object's build path as a /proc/self/cwd-relative path
    # (e.g. /proc/self/cwd/bazel-out/k8-fastbuild/bin/src/.../foo.pic.gcda) and
    # writes the .gcda there at process exit, resolved against the *current*
    # cwd.  runqa runs each test with cwd = the test directory
    # (qa/<category>/), so every fly process — master and workers alike —
    # dumps its gcda under qa/<category>/bazel-out/..., NOT under bazel-bin/.
    # lcov --capture below only scans bazel-bin/_objs, so all that data was
    # invisible.  That is what made whole modules report 0%.
    #
    # Fix: GCOV_PREFIX + GCOV_PREFIX_STRIP redirect the gcda writes to a fixed
    # location regardless of cwd.  GCOV_PREFIX_STRIP=3 strips the
    # "/proc/self/cwd" prefix from the recorded build path, and GCOV_PREFIX
    # points at the execroot (the real directory bazel-bin symlinks into), so
    # gcda land exactly where the .gcno already live and where lcov scans.
    # Both master and worker processes inherit these env vars and write to the
    # same execroot tree.
    #
    # gcda update is a non-atomic read-merge-write on process exit.  Run QA
    # serially (-j 1) so concurrent fly processes can't stomp each other's
    # gcda at shutdown.  Coverage measurement is an occasional diagnostic, so
    # the slowdown is acceptable.
    info "Running QA integration tests with coverage (serial, gcda -> execroot)..."
    "$FLY_ROOT/fly.sh" install
    local EXECROOT
    EXECROOT="$(readlink -f "$BAZEL_BIN" | sed 's|/bazel-out/.*||')"
    GCOV_PREFIX="$EXECROOT" GCOV_PREFIX_STRIP=3 \
        bash "$FLY_ROOT/qa/run_qa_tests.sh" -j 1 2>&1 | tail -5 \
        || warn "QA had failures — capturing coverage from passed cases anyway"

    # ── Collect coverage data ──
    info "Collecting C++ coverage data with lcov..."

    local OBJ_DIRS=""
    for dir in agent storage network task core log serialization common main; do
        local d="$BAZEL_BIN/src/$dir/cpp/_objs"
        if [ -d "$d" ]; then
            OBJ_DIRS="$OBJ_DIRS --directory $d"
        fi
    done

    # lcov 2.x compatibility:
    #   - RC key renamed lcov_branch_coverage -> branch_coverage (old name is a
    #     hard error now).
    #   - "empty" (no branch coverpoints in a TU) and "deprecated" (old RC
    #     keys) and "source" (missing /proc/self/cwd paths) are hard errors by
    #     default; downgrade them with --ignore-errors.
    lcov --capture $OBJ_DIRS \
        --output-file "$cpp_dir/coverage_raw.info" \
        --rc branch_coverage=1 \
        --gcov-tool /usr/bin/gcov-12 \
        --ignore-errors empty,deprecated,source,inconsistent,negative 2>&1 | tail -3

    # Filter system headers and test files
    lcov --remove "$cpp_dir/coverage_raw.info" \
        '/usr/include/*' \
        '*/external/*' \
        '*/tests/*' \
        '*/test_*' \
        '*/export/*' \
        --rc branch_coverage=1 \
        --ignore-errors empty,deprecated,source,inconsistent,negative \
        --output-file "$cpp_dir/coverage.info" 2>&1 | tail -3

    # Fix source paths (Bazel uses /proc/self/cwd/ prefix)
    sed -i 's|/proc/self/cwd/|'"$FLY_ROOT"'/|g' "$cpp_dir/coverage.info"

    # Generate HTML report.
    # lcov 2.x flags gcov's "function not hit but line hit" data as an
    # "inconsistent" hard error; --ignore-errors downgrades it so genhtml can
    # still render. This is a known gcov-vs-lcov fidelity gap, not data loss.
    genhtml "$cpp_dir/coverage.info" \
        --branch-coverage --legend \
        --ignore-errors inconsistent \
        --output-directory "$cpp_dir/html" 2>&1 | tail -5

    # Print summary
    echo ""
    info "C++ Coverage Summary:"
    lcov --summary "$cpp_dir/coverage.info" --rc branch_coverage=1 \
        --ignore-errors inconsistent 2>&1 | grep -E "(lines|functions|branches)"
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
