"""E2E 回归测试：merge 后 master 的 DB 路径权威源一致（db_instances_ 收敛）。

审计背景：曾存在三套 DB 路径副本（db_registry_ 字符串表 / db_instances_ Database 内嵌 /
DataService::db_paths_），merge 后 cleanup_after_merge 只更新 db_registry_，导致
db_instances_ 的 Database 永久持旧路径。修复后 Database 是 master 路径唯一权威源，
merge 后经 set_paths 同步更新。

本测试验证修复后的可观察行为：
  merge 后启动一个【全新 host_C worker】（本地无任何数据缓存），对它提交读 task。
  host_C 首次访问该 db 必经 DbPathRequest 向 master 询问路径：
    - 若 master 返回旧源 data_path（delete_source=True 后已删）→ host_C 构造的
      Database 指向不存在目录 → 读失败 / task 失败
    - 若 master 返回 merge 新路径（merged_data，数据所在）→ 读成功
  所以"host_C 上读 task 成功"即证明 master 路径权威源已正确指向 merge 新路径。

流程：
  host_A/host_B 写对象 → freeze → merge_db(delete_source=True)
  → launch 全新 host_C + 打 host_c_reader 属性 → 读 task 强制调度到 host_C → 校验读到
"""
from _fly_log import INFO
import os
import time
import shutil

from fly import as_task, open_db, merge_db, get_config
from test import write_data

DB_PATH = os.path.join(get_config().get_str("log_dir"), "merge_paths_db")
from fly.runtime import get_agent


def cleanup():
    for p in [DB_PATH, DB_PATH + ".merged_data"]:
        if os.path.isdir(p):
            shutil.rmtree(p, ignore_errors=True)


def wait_for(condition, timeout=30.0, interval=0.3):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if condition():
            return True
        time.sleep(interval)
    return False


# 读 task：声明全部对象为数据依赖（确保对象就绪后才执行），requires 强制调度到 host_C。
# host_C 执行时必经 DbPathRequest 问 master 路径 → 验证 master 返回的是 merge 新路径。
@as_task(inputs=lambda db, keys, expected: [db.get_full_name(k) for k in keys],
         requires=["host_c_reader"])
def read_on_host_c(db, keys, expected):
    for k, exp in zip(keys, expected):
        val = db.read_object(k)
        if val != exp:
            return False
    return True


cleanup()
get_config().set_int("fail_unscheduleable_tasks", 0)

master = get_agent()
# host_A / host_B 作为数据源（模拟多机分散数据）。
master.launch_local_workers([{"host": "host_A"}, {"host": "host_B"}])
assert master.wait_workers_registered(timeout=60), "need host_A + host_B workers"

db = open_db(DB_PATH)
write_data(db, "paths/k1", 111)
write_data(db, "paths/k2", 222)
write_data(db, "paths/k3", "consistency")
assert wait_for(lambda: len(master.completed_tasks) >= 3)
time.sleep(0.5)  # 等落盘

db.freeze()
assert db.is_frozen()

# 记录源 .dat 数（merge + delete_source 后应清零，证明旧 data_path 已失效）。
def count_dat(path):
    if not os.path.isdir(path):
        return 0
    return sum(1 for f in os.listdir(path) if f.startswith("data_") and f.endswith(".dat"))

src_dat_before = count_dat(DB_PATH)
assert src_dat_before > 0, "source should have .dat before merge"

INFO("[PATHS] calling merge_db (delete_source=True)")
merged_db = merge_db(DB_PATH, delete_source=True)
merged_data_path = DB_PATH + ".merged_data"
INFO(f"[PATHS] merge done, merged_data_path={merged_data_path}")

# 源 data_path 的 .dat 应已删除（旧路径失效）。
assert count_dat(DB_PATH) == 0, "source .dat should be deleted after merge"
# 产物 data_path 应有 .dat（新路径有效）。
assert count_dat(merged_data_path) > 0, "merged data_path should have .dat"

# ── 关键验证：launch 全新 host_C（本地无数据缓存）+ 启动即带 host_c_reader 属性 ──
# host_C 读对象时必经 DbPathRequest 询问 master 路径。master 若回旧源路径（已删）则
# 构造的 Database 指向不存在目录；回新 merge 路径则读成功。
# requires=["host_c_reader"] 是子集匹配，强制 read task 只能调度到带该属性的 host_C。
master.launch_local_workers([{"host": "host_C", "attributes": ["host_c_reader"]}])
assert master.wait_workers_registered(timeout=60), "host_C worker should connect"
INFO("[PATHS] host_C worker launched (fresh, no local data, with host_c_reader attr)")

# 提交读 task（requires 强制到 host_C）。
# host_C 读不到 → DbPathRequest 路径错 → read_object 抛异常 → TaskFailed。
keys = ["paths/k1", "paths/k2", "paths/k3"]
expected = [111, 222, "consistency"]
read_on_host_c(merged_db, keys, expected)
INFO("[PATHS] submitted read_on_host_c task (forced to host_C)")

completed = master.wait_for_all_tasks(timeout=30)
INFO(f"[PATHS] host_C read task completed, total tasks={len(completed)}")

# 额外校验：master 句柄直接读（走 remote_idx → merge worker）。
assert merged_db.read_object("paths/k1") == 111
assert merged_db.read_object("paths/k2") == 222
assert merged_db.read_object("paths/k3") == "consistency"

INFO("[PASS] test_merge_db_paths_consistency")
