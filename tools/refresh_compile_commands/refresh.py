#!/usr/bin/env python3
"""Generate compile_commands.json from Bazel action graph.

Replaces hedron_compile_commands which depends on native.py_binary (removed in Bazel 9).
"""

import json
import os
import subprocess
import sys


def run_bazel_aquery(targets):
    """Run bazel aquery to get CppCompile actions for target deps."""
    dep_queries = " + ".join(f'deps("{t}")' for t in targets)
    query = f'mnemonic("CppCompile", {dep_queries})'
    cmd = [
        "bazel", "aquery", query,
        "--output=jsonproto",
        "--include_artifacts=false",
        "--include_param_files=false",
    ]
    result = subprocess.run(cmd, capture_output=True, text=True, cwd=os.getcwd())
    if result.returncode != 0:
        print(f"ERROR: bazel aquery failed:\n{result.stderr}", file=sys.stderr)
        sys.exit(1)
    return json.loads(result.stdout)


def parse_compile_command(arguments, workspace_root):
    """Parse one CppCompile action's arguments into a compile_commands.json entry."""
    compiler = arguments[0]
    source_file = None
    clang_args = []

    i = 0
    while i < len(arguments):
        arg = arguments[i]
        if arg == "-c":
            if i + 1 < len(arguments):
                source_file = arguments[i + 1]
            i += 2
            continue
        if arg == "-o":
            i += 2
            continue
        if arg.startswith("-MF") or arg.startswith("-MQ") or arg.startswith("-MT"):
            i += 1
            continue
        if arg.startswith("@"):
            i += 1
            continue
        clang_args.append(arg)
        i += 1

    if not source_file:
        return None

    if not os.path.isabs(source_file):
        source_file = os.path.join(workspace_root, source_file)

    return {
        "directory": workspace_root,
        "command": " ".join([compiler] + clang_args),
        "file": os.path.normpath(source_file),
    }


def generate_compile_commands(targets, output_path):
    """Main entry point: generate compile_commands.json from bazel action graph."""
    workspace_root = os.getcwd()

    print(f"Querying {len(targets)} targets for compile commands...", file=sys.stderr)

    aquery_json = run_bazel_aquery(targets)

    entries = []
    for action in aquery_json.get("actions", []):
        arguments = action.get("arguments", [])
        if not arguments:
            continue
        entry = parse_compile_command(arguments, workspace_root)
        if entry:
            entries.append(entry)

    # Deduplicate by normalized file path
    seen = set()
    unique_entries = []
    for entry in entries:
        path = entry["file"]
        if path not in seen:
            seen.add(path)
            unique_entries.append(entry)

    unique_entries.sort(key=lambda e: e["file"])

    with open(output_path, "w") as f:
        json.dump(unique_entries, f, indent=2)

    print(f"Generated {output_path} with {len(unique_entries)} entries", file=sys.stderr)
    return 0


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description="Generate compile_commands.json")
    parser.add_argument("--targets", nargs="+", required=True,
                        help="Bazel targets to include")
    parser.add_argument("--output", default="compile_commands.json",
                        help="Output file path")
    args = parser.parse_args()

    sys.exit(generate_compile_commands(args.targets, args.output))
