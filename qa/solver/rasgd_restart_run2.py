"""rasgd restart 场景 run2：全新 run（新 master + 新 worker，进程缓存全空）
从 failed_tasks.bin 断点续跑。

验证：
  1. load_db 恢复（sol_0/sol_1 + temp 对象 b_2/sub 等）
  2. restart_failed_tasks 重投失败组 → 链自动恢复推进至 T 步完成
  3. 已有结果未被重算改写（md5 与 run1 一致）
  4. 冷启动：新 worker 上 LDLT 重建（恰 nsd 次）后求解结果仍正确
"""
import os

FLY_CASE_LOG_DIR = os.environ["FLY_CASE_LOG_DIR"]
import json
import hashlib

import numpy as np
from scipy import sparse
from scipy.sparse.linalg import splu

from _fly_log import INFO
from fly import load_db, get_config
from fly.runtime import get_agent
from solver import get_dynamic_result

N_SIDE = 20
NSD = 4
NUM_STEPS = 3
DB_PATH = os.environ["FLY_DB_PATH"]

get_config().set_int("fail_unscheduleable_tasks", 1)

db = load_db(DB_PATH)

# 全新 run 的 worker 池（attributes 必须与原 run 一致——task requires 匹配）
master = get_agent()
worker_configs = []
for w in range(NSD):
    worker_configs.append({"attributes": [f"sd_{w}"]})
worker_configs.append({"attributes": ["ras_check"]})
master.launch_local_workers(worker_configs)
assert master.wait_for_workers(NSD + 1)

restarted = master.restart_failed_tasks([db])
INFO(f"restart_failed_tasks resubmitted: {restarted}")
assert restarted > 0, "no failed tasks were resubmitted"

result = get_dynamic_result(db, timeout=240)
INFO(f"dynamic result after restart: {result}")
assert result["num_steps_done"] == NUM_STEPS
assert all(result["converged"])

# 已有结果未被重算改写
with open(os.path.join(FLY_CASE_LOG_DIR, "restart_digests.json")) as f:
    digests = json.load(f)
for name, expect_md5 in digests.items():
    x_t = np.asarray(db.read_object(name))
    actual = hashlib.md5(x_t.tobytes()).hexdigest()
    assert actual == expect_md5, \
        f"{name} was recomputed/changed: {actual} != {expect_md5}"
INFO(f"preserved results intact: {list(digests)}")

# 数值正确性（全部 T 步）
_data_md = db.read_object("__rasg__matrix")
A_sp = sparse.csc_matrix((_data_md["vals"], (_data_md["rows"], _data_md["cols"])),
                         shape=(int(_data_md["N"]), int(_data_md["N"])))
b0 = np.asarray(_data_md["b"], dtype=np.float64)
lu = splu(A_sp)
for t in range(NUM_STEPS):
    x_t = np.asarray(db.read_object(f"__rasg__sol_{t}"))
    x_exact = lu.solve(b0 * (1.0 + 0.1 * t))
    rel = np.linalg.norm(x_t - x_exact) / np.linalg.norm(x_exact)
    assert rel < 1e-4, f"t={t} rel_err too large: {rel:.2e}"


get_agent().stop()
# 冷启动：新 worker 上 LDLT 重建恰 nsd 次（重投的 compute 组）
setup_count = 0
sub_dir = os.environ["FLY_CASE_SUB_DIR"]
for fn in sorted(os.listdir(sub_dir)):
    if fn.startswith("worker") and fn.endswith(".log"):
        with open(os.path.join(sub_dir, fn), errors="replace") as f:
            setup_count += f.read().count("LDLT setup done (cold)")
INFO(f"LDLT cold setups in fresh run: {setup_count} (expect {NSD})")
assert setup_count == NSD, f"cold setups={setup_count} (expect {NSD}); files=" + str([(fn, open(os.path.join(sub_dir, fn), errors="replace").read().count("LDLT setup done (cold)")) for fn in sorted(os.listdir(sub_dir)) if fn.startswith("worker") and fn.endswith(".log")])

INFO("[PASS] rasgd_restart_run2 (chain resumed from bin, results correct)")
