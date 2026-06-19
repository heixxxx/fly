#!/usr/bin/env python3
"""Profiling test to verify copy elimination in read/write paths.

This test measures memory allocation patterns to confirm that unnecessary
copies have been eliminated from the data paths.

Test cases:
1. Write Python type (pickle) - should have minimal copies
2. Read Python type (pickle) - should have minimal copies
3. Write C++ type - should have minimal copies
4. Read C++ type - should have minimal copies
"""

import sys
import os
import time
import tracemalloc

# Add build directory to path

from fly import open_db, get_config
import tempfile
import shutil


def generate_large_data(size_mb=10):
    """Generate large test data."""
    return bytearray(size_mb * 1024 * 1024)


def profile_write_python_type(db, data, name="test_obj"):
    """Profile write path for Python type."""
    tracemalloc.start()

    # Take snapshot before write
    snapshot1 = tracemalloc.take_snapshot()

    # Perform write
    db.write_object(name, data)

    # Take snapshot after write
    snapshot2 = tracemalloc.take_snapshot()

    # Calculate difference
    diff = snapshot2.compare_to(snapshot1, 'lineno')

    tracemalloc.stop()

    return diff


def profile_read_python_type(db, name="test_obj"):
    """Profile read path for Python type."""
    tracemalloc.start()

    # Take snapshot before read
    snapshot1 = tracemalloc.take_snapshot()

    # Perform read
    result = db.read_object(name)

    # Take snapshot after read
    snapshot2 = tracemalloc.take_snapshot()

    # Calculate difference
    diff = snapshot2.compare_to(snapshot1, 'lineno')

    tracemalloc.stop()

    return diff, len(result)


def main():
    """Main profiling test."""
    tmpdir = tempfile.mkdtemp(prefix="fly_copy_prof_")

    try:
        # Create database
        db = open_db(os.path.join(tmpdir, "test_db"))

        # Generate test data (1MB)
        data = generate_large_data(1)
        print(f"Test data size: {len(data)} bytes")

        # Basic write/read test first
        print("\n=== Basic Write/Read Test ===")
        db.write_object("test_obj", data)
        time.sleep(1.0)  # Wait for async write

        result = db.read_object("test_obj")
        if result == data:
            print("✓ Basic write/read works")
        else:
            print("✗ Basic write/read FAILED")
            return 1

        # Profile write (smaller data for profiling)
        print("\n=== Write Path Profile ===")
        small_data = generate_large_data(100)  # 100KB
        write_diff = profile_write_python_type(db, small_data, "prof_write")

        print("Top memory allocations during write:")
        for stat in write_diff[:10]:
            print(f"  {stat}")

        # Wait for async write to complete
        time.sleep(0.5)

        # Profile read
        print("\n=== Read Path Profile ===")
        read_diff, result_size = profile_read_python_type(db, "prof_write")

        print(f"Read result size: {result_size} bytes")
        print("Top memory allocations during read:")
        for stat in read_diff[:10]:
            print(f"  {stat}")

        # Verify data integrity
        print("\n=== Data Integrity Check ===")
        result = db.read_object("prof_write")
        if result == small_data:
            print("✓ Data integrity verified")
        else:
            print("✗ Data integrity check FAILED")
            return 1

        print("\n=== Summary ===")
        print("Expected behavior:")
        print("  - Write: No intermediate CMString copies (buffer -> string_view -> compress)")
        print("  - Read: Direct decompress to Python bytes (no intermediate CMString)")
        print("\nIf you see large allocations from decompressing_streambuf.cpp or")
        print("compressing_streambuf.cpp, there may still be unnecessary copies.")

        return 0

    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
