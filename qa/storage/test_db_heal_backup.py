"""db 链补洞 / backup 副本 / var Python 对象往返 / find_all_dbs 深链。

覆盖（2026-09 覆盖率批次 14 项之 4）：
  - A→B 链手工删除 A.next → load_db(B) 触发 _heal_next_edges 回填
  - db.backup_object 生成 backup 副本 → read_object(backup=True) 读回一致
  - set_var/get_var：Python 对象（pickle 路径）往返 + miss 语义
  - find_all_dbs 深链（BFS 距离序）+ 按条件过滤
"""
import os
import shutil

from _fly_log import INFO

from fly import open_db, load_db
from storage import DbMetaFile
from test import wait_until

CASE_DIR = os.environ["FLY_CASE_LOG_DIR"]
BASE = os.path.join(CASE_DIR, "heal_backup")
PATH_A = os.path.join(BASE, "a")
PATH_B = os.path.join(BASE, "b")

if os.path.isdir(BASE):
    shutil.rmtree(BASE, ignore_errors=True)
os.makedirs(BASE, exist_ok=True)

from fly.runtime import get_agent
master = get_agent()

# ── 1. 链补洞：手删 A.next → load_db(B) 回填 ────────────────────────
db_a = open_db(PATH_A)
db_b = open_db(PATH_B, prev=[db_a])
b_uid = db_b.get_uid()
assert b_uid

a_chain_before = DbMetaFile(PATH_A).read()
assert [e["uid"] for e in a_chain_before["next"]] == [b_uid]

# 模拟建链 crash：上游 next 边丢失（prev 是权威边，next 是可重建缓存）
DbMetaFile(PATH_A).update(lambda d: {**d, "next": []})
assert DbMetaFile(PATH_A).read()["next"] == []

healed = load_db(PATH_B)  # load_db 尾部调 _heal_next_edges
assert healed.get_uid() == b_uid

a_chain_after = DbMetaFile(PATH_A).read()
healed_edges = [e for e in a_chain_after["next"] if e["uid"] == b_uid]
assert len(healed_edges) == 1, f"A.next 必须被回填: {a_chain_after}"
assert healed_edges[0]["db_path"] == PATH_B
INFO("[PASS] load_db heals missing upstream next edge")

db = healed  # 后续段落的操作句柄（load_db 返回的权威 Database）

# ── 2. backup 副本（写时 backup=True；backup_object 放弃语义）────────
# 注：db.backup_object 与源 write 同 task 同名会被 provenance 校验拒绝并撤销
# 写入段（零容忍数据安全语义）；带副本的正规写路径是 write_object(backup=True)。
master.launch_local_workers([{}])
assert wait_until(lambda: master.worker_count >= 1, timeout=30), "worker must connect"

import time
from fly import as_task


@as_task()
def write_with_backup(db):
    db.write_object("backed_up", {"payload": 41}, backup=True)


write_with_backup(healed)
t0 = time.time()
while len(master.completed_tasks) < 1:
    assert not master.failed_tasks, \
        f"write(backup=True) task failed: {master.get_task_error(master.failed_tasks[0])}"
    assert time.time() - t0 < 60, "write task must finish"
    time.sleep(0.1)
assert healed.read_object("backed_up") == {"payload": 41}, "带副本写后源必须可读"

# backup_object 对不可见对象：尽力语义（ERR + 放弃），不抛
healed.backup_object("never_written_obj")
INFO("[PASS] write_object(backup=True) + backup_object abandon semantics")

# ── 3. var Python 对象分支往返 + miss 语义 ───────────────────────────
# var 是 Python 业务侧轻量对象 API（2026-09-02 裁定）：C++ 导出对象分支
# 已随裁定删除（原分支按 type_name 反查 _fly_storage 必败，见 fix 记录）。
db.set_var("py_obj", {"k": [1, 2]})
assert db.get_var("py_obj") == {"k": [1, 2]}
db.set_var("py_str", "hello_var")
assert db.get_var("py_str") == "hello_var"
assert db.get_var("never_set") is None, "不存在的 var 应返回 None"
INFO("[PASS] set_var/get_var python-object roundtrip + miss returns None")

# ── 4. find_all_dbs 深链 ────────────────────────────────────────────
path_c = os.path.join(BASE, "c")
path_d = os.path.join(BASE, "d")
db_c = open_db(path_c, prev=[db])
db_d = open_db(path_d, prev=[db_c])

found = db_d.find_all_dbs()
uids = [x.get_uid() for x in found]
assert uids == [db_c.get_uid(), db.get_uid(), db_a.get_uid()], \
    f"BFS 距离序应为 C,B,A: {uids}"

# 按条件过滤：只命中 A（logical_name；find_all_dbs 不含 uid 维度——仅 find_db 有）
by_name = db_d.find_all_dbs(logical_name="a")
assert [x.get_uid() for x in by_name] == [db_a.get_uid()]
by_role = db_d.find_all_dbs(logical_name="c")
assert [x.get_uid() for x in by_role] == [db_c.get_uid()]
# 无匹配 → 空
assert db_d.find_all_dbs(logical_name="nope") == []
INFO("[PASS] find_all_dbs deep chain BFS order + filtering")

master.stop()
INFO("[PASS] test_db_heal_backup")
