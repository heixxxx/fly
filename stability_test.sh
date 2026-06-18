#!/bin/bash
set -e  # Exit on any error

cd /root/fly

total_rounds=50
passed_rounds=0

echo "=== Starting 50-round full stability test ==="

for round in $(seq 1 $total_rounds); do
    echo "=== Running round $round/$total_rounds ==="

    LD_LIBRARY_PATH=build/lib:$LD_LIBRARY_PATH PYTHONPATH=build/lib:build/python:$PYTHONPATH timeout 120 python3 -c "
import time
import random
import tempfile
import shutil
import os

from fly import open_db, as_task, launch_workers, wait_tasks

tmpdir = tempfile.mkdtemp(prefix=f'stability_{os.getpid()}_')

try:
    # Create database
    db = open_db(os.path.join(tmpdir, 'test_db'))

    # Test 1: bytes data
    data_bytes = b'x' * random.randint(1024, 1024*1024)
    db.write_object('bytes', data_bytes)
    time.sleep(0.1)
    result = db.read_object('bytes')
    assert result == data_bytes, f'Bytes mismatch'

    # Test 2: list data
    data_list = [random.randint(0, 1000) for _ in range(random.randint(100, 10000))]
    db.write_object('list', data_list)
    time.sleep(0.1)
    result = db.read_object('list')
    assert result == data_list, f'List mismatch'

    # Test 3: dict data
    data_dict = {f'key_{i}': random.randint(0, 1000) for i in range(random.randint(10, 100))}
    db.write_object('dict', data_dict)
    time.sleep(0.1)
    result = db.read_object('dict')
    assert result == data_dict, f'Dict mismatch'

    # Test 4: multiple writes
    for i in range(5):
        data = b'test_' + str(i).encode() * random.randint(100, 10000)
        db.write_object(f'multi_{i}', data)

    time.sleep(0.2)

    # Test 5: freeze
    db.freeze()

    print(f'Round passed')
except Exception as e:
    print(f'ERROR: {e}')
    raise
finally:
    shutil.rmtree(tmpdir, ignore_errors=True)
"

    if [ $? -eq 0 ]; then
        passed_rounds=$((passed_rounds + 1))
        echo "Round $round PASSED"
    else
        echo "Round $round FAILED - stopping"
        echo "Total passed: $passed_rounds/$total_rounds"
        exit 1
    fi
done

echo "=== ALL $total_rounds ROUNDS PASSED ==="
echo "Total passed: $passed_rounds/$total_rounds"
exit 0
