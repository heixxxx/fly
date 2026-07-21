package(default_visibility = ["//visibility:public"])

# =============================================================================
# Aggregated third-party Python packages for the installed `fly` binary.
# =============================================================================
#
# Production `fly` is a cc_binary that embeds libpython; it does NOT use bazel
# runfiles. `fly.sh install` therefore copies these unpacked wheels into
# build/python/lib/python3.12/site-packages/ so setup_sys_path() in main.cpp can
# add them to sys.path at runtime. This makes the production binary hermetic —
# it no longer imports cloudpickle/numpy/scipy from the system site-packages.
#
# Add a package here when production src/ code (not just tests) imports it.
# fly.sh install reads this target via `bazel cquery @labels --output=files`.
#
# Per-package we pull two sub-targets:
#   :pkg        — the importable files (e.g. cloudpickle/__init__.py)
#   :dist_info  — the .dist-info metadata (needed by some tools / inspect)

alias(
    name = "fly_third_party_py",
    actual = ":_fly_third_party_py_impl",
)

filegroup(
    name = "_fly_third_party_py_impl",
    srcs = [
        # cloudpickle — user-task / mapreduce fn serialization
        "@pip//cloudpickle:pkg",
        "@pip//cloudpickle:dist_info",
        # numpy / scipy — distributed RAS solver (src/solver/py/ras_graph.py)
        "@pip//numpy:pkg",
        "@pip//numpy:dist_info",
        "@pip//scipy:pkg",
        "@pip//scipy:dist_info",
    ],
)
