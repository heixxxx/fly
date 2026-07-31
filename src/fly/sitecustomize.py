"""Auto-imported at Python interpreter start-up to start coverage measurement.

This module is the fix for the coverage methodology flaw documented in
docs/coverage-testing.md §12.1 and coverage-report-2026-07-31.md §4.1: the
old code called ``coverage.start()`` from inside ``fly.main._run_master`` /
``_run_worker``, but by then the ``fly`` package (and all its transitive
imports: storage/core/task/userdoc/mapreduce/project) plus the C++ binding
imports had already executed — unmeasured.  Hence fly/__init__.py showed a
bogus 36%, main.py 11%, bootstrap.py 31%.

How it works
------------
CPython runs ``import site`` during ``Py_Initialize`` (main.cpp uses plain
``Py_Initialize`` with default config).  ``site`` then auto-imports the first
``sitecustomize`` module found on ``sys.path``.  The fly wrapper (fly.sh) puts
``build/python`` on ``PYTHONPATH``, so a ``sitecustomize.py`` living there
runs *before any* ``import fly`` — exactly when coverage must hook in.

This module is a no-op unless ``FLY_PYCOVERAGE`` is set, so normal fly runs
pay zero cost.  When enabled, it points coverage.py at ``.coveragerc`` (via
``COVERAGE_PROCESS_START``) and calls ``coverage.process_startup()``, the
official multi-process entry point.  Worker processes inherit the env var
and start their own coverage the same way — no special spawn-time wiring.

On failure we write a diagnostic to /tmp and never raise: a broken
sitecustomize would break *every* fly invocation, which is unacceptable for a
measurement-time-only feature.
"""

import os

# Only act when explicitly requested.  No env var => zero overhead no-op.
if os.environ.get("FLY_PYCOVERAGE"):
    try:
        # .coveragerc lives alongside this sitecustomize.py in build/python/
        # (install layout) or src/fly/ (source tree).  Prefer the file next
        # to this module so the two stay in sync regardless of layout.
        _HERE = os.path.dirname(os.path.abspath(__file__))
        _RC = os.path.join(_HERE, ".coveragerc")
        if not os.path.exists(_RC):
            # Source-tree fallback (when run directly out of src/fly).
            _RC = os.path.join(_HERE, "src", "fly", ".coveragerc")

        if os.path.exists(_RC):
            # coverage.process_startup() keys off COVERAGE_PROCESS_START
            # (the path to the rcfile).  Setting it here (rather than in the
            # spawning shell) keeps the coverage config self-contained and
            # makes workers pick it up automatically via env inheritance.
            os.environ["COVERAGE_PROCESS_START"] = _RC

            import coverage
            coverage.process_startup()
    except Exception as _e:
        # Never let a measurement feature break the interpreter boot.
        try:
            role = "worker" if os.environ.get("FLY_WORKER_ID") else "master"
            with open("/tmp/fly_{}_cov_error.txt".format(role), "a") as _f:
                _f.write("sitecustomize failed: {}\n".format(_e))
        except Exception:
            pass
