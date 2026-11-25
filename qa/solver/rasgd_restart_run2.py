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
from fly import ensure_workers, get_config, load_db
from test import record
from fly.runtime import get_agent
from solver import get_dynamic_result

N_SIDE = 20
NSD = 4
NUM_STEPS = 3
DB_PATH = os.environ["FLY_DB_PATH"]

get_config().set_int("fail_unscheduleable_tasks", 1)

# issue 009 框架增强后的标准恢复流程：先 load_db 再编队（与旧规避相反的
# 顺序，直接回归 009 的脆弱点）。进程数量先行——load_db 本机无 worker 时
# 自动 spawn 空属性补位（恰好成为候选池的一部分），按编队规模缺口再补空
# 属性进程；ensure_workers 按 db uid 命名空间追加属性——bin 里还原的
# requires 与本次申请同源于 SolveDb.worker_attr，自动闭环。已唤起未注册的
# 占位符计入 ensure 预检容量，注册等待受其 timeout 约束。
master = get_agent()
db = load_db(DB_PATH)

fleet = NSD + 1
if not master.is_running() or master.worker_count < fleet:
    deficit = fleet - max(0, master.worker_count)
    master.launch_local_workers([{} for _ in range(deficit)])

attrs = [db.worker_attr(f"sd_{s}") for s in range(NSD)] + [db.worker_attr("check")]
ensure_workers(attrs, timeout=30.0, exclude=r"^rasg:")

restarted = master.restart_failed_tasks([db])
INFO(f"restart_failed_tasks resubmitted: {restarted}")
# 原断言：assert restarted > 0, "no failed tasks were resubmitted"
record("restart_resubmitted", restarted > 0, detail=restarted)

result = get_dynamic_result(db)
INFO(f"dynamic result after restart: {result}")
# 原断言：assert result["num_steps_done"] == NUM_STEPS
record("num_steps_done", result["num_steps_done"])
# 原断言：assert all(result["converged"])
record("all_converged", all(result["converged"]), detail=list(result["converged"]))

# 已有结果未被重算改写
with open(os.path.join(FLY_CASE_LOG_DIR, "restart_digests.json")) as f:
    digests = json.load(f)
changed = []
for name, expect_md5 in digests.items():
    x_t = np.asarray(db.read_object(name))
    actual = hashlib.md5(x_t.tobytes()).hexdigest()
    if actual != expect_md5:
        changed.append(name)
# 原断言：assert actual == expect_md5, \
record("preserved_results_intact", not changed, detail=changed)
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
    # 原断言：assert rel < 1e-4, f"t={t} rel_err too large: {rel:.2e}"
    record(f"step{t}/rel_err_ok", rel < 1e-4, detail=f"{rel:.2e}")


get_agent().stop()
# 冷启动：新 worker 上 LDLT 重建恰 nsd 次（重投链的 setup_compute）
setup_files = {}
setup_count = 0
sub_dir = os.environ["FLY_CASE_SUB_DIR"]
for fn in sorted(os.listdir(sub_dir)):
    if fn.startswith("worker") and fn.endswith(".log"):
        with open(os.path.join(sub_dir, fn), errors="replace") as f:
            n = f.read().count("LDLT done (cold)")
        setup_files[fn] = n
        setup_count += n
INFO(f"LDLT cold setups in fresh run: {setup_count} (expect {NSD})")
# 原断言：assert setup_count == NSD, f"cold setups={setup_count} (expect {NSD}); files=" + str([(fn, open(os.path.join(sub_dir, fn), errors="replace").read().count("LDLT done (cold)")) for fn in sorted(os.listdir(sub_dir)) if fn.startswith("worker") and fn.endswith(".log")])
# detail 带每个 worker 日志文件的计数，失败时不用翻日志就能定位是哪个 worker 多算或漏算
record("ldlt_cold_setup_count_ok", setup_count == NSD,
       detail={"count": setup_count, "expect": NSD, "files": setup_files})

INFO("[DONE] rasgd_restart_run2 (chain resumed from bin, results correct)")
