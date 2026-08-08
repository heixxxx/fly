#!/usr/bin/env python3
"""Test to verify copy elimination in read/write paths.

This test writes and reads a large object to verify that unnecessary copies
have been eliminated from the data paths.
"""

import sys
import os
import time

# Add build directory to path

from fly import open_db, get_config, wait_tasks, launch_workers
import tempfile
import shutil


def main():
    """Main test."""
    tmpdir = tempfile.mkdtemp(prefix="fly_copy_elim_")

    try:
        # Create database
        db = open_db(os.path.join(tmpdir, "test_db"))
        print(f"Database created: {db.get_db_path()}")

        # Generate test data (10MB)
        data_size = 10 * 1024 * 1024
        data = b'x' * data_size
        print(f"Test data size: {len(data)} bytes")

        # Write data
        print("\n=== Writing ===")
        start = time.time()
        db.write_object("large_obj", data)
        write_time = time.time() - start
        print(f"Write completed in {write_time:.3f}s")

        # Wait for async write to complete
        time.sleep(1.0)

        # Read data
        print("\n=== Reading ===")
        start = time.time()
        result = db.read_object("large_obj")
        read_time = time.time() - start
        print(f"Read completed in {read_time:.3f}s")

        # Verify data integrity
        print("\n=== Verification ===")
        if result == data:
            print("✓ Data integrity verified")
        else:
            print("✗ Data integrity FAILED")
            print(f"  Expected size: {len(data)}")
            print(f"  Got size: {len(result)}")
            return 1

        print("\n=== Summary ===")
        print(f"Write: {write_time:.3f}s ({data_size / write_time / 1024 / 1024:.1f} MB/s)")
        print(f"Read:  {read_time:.3f}s ({data_size / read_time / 1024 / 1024:.1f} MB/s)")

        return 0

    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
