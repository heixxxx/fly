#!/bin/bash
set -e  # Exit on any error

cd /root/fly

total_rounds=50
passed_rounds=0

echo "=== Starting 50-round stability test ==="

for round in $(seq 1 $total_rounds); do
    echo "=== Running round $round/$total_rounds ==="

    timeout 120 bash -c "
        import subprocess, time, random, sys, os
        port = random.randint(15000, 16000)
        procs = []
        # Start master
        procs.append(subprocess.Popen(['build/bin/fly', '--master', '--port', str(port), '--host', 'localhost'], stdout=subprocess.PIPE, stderr=subprocess.STDOUT))
        time.sleep(2)
        # Start 4 workers
        for i in range(4):
            procs.append(subprocess.Popen(['build/bin/fly', '--worker', '--master-host', 'localhost', '--master-port', str(port), '--host', f'w{i}', '--num-shards', '1'], stdout=subprocess.PIPE, stderr=subprocess.STDOUT))
        time.sleep(3)
        # Run test
        r = subprocess.run([sys.executable, '-c', '''
from fly import FlyAgent, as_task
import numpy as np
agent = FlyAgent()
A = np.random.randn(20, 20)
b = np.random.randn(20)
@as_task
def solve(A, b):
    return np.linalg.solve(A, b).tolist()
result = agent.mapreduce(solve, [(A, b)], num_shards=4)
print(f\"RESULT: {len(result)}\")
assert len(result) == 1
agent.shutdown()
'''], capture_output=True, text=True, timeout=60)
        print(r.stdout)
        if r.returncode != 0:
            print(r.stderr)
            sys.exit(1)
        # Kill all
        for p in procs:
            p.kill()
            p.wait()
    "

    if [ $? -eq 0 ]; then
        passed_rounds=$((passed_rounds + 1))
        echo "Round $round PASSED"
    else
        echo "Round $round FAILED - stopping"
        exit 1
    fi
done

echo "=== ALL $total_rounds ROUNDS PASSED ==="
echo "Total passed: $passed_rounds/$total_rounds"
exit 0
