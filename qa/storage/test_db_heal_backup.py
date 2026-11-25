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

from test import record
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
# 原断言：assert b_uid
record("chain/b_uid_present", bool(b_uid), detail=b_uid)

a_chain_before = DbMetaFile(PATH_A).read()
next_uids_before = [e["uid"] for e in a_chain_before["next"]]
# 原断言：assert [e["uid"] for e in a_chain_before["next"]] == [b_uid]
record("chain/next_edge_is_b", next_uids_before == [b_uid], detail=a_chain_before)

# 模拟建链 crash：上游 next 边丢失（prev 是权威边，next 是可重建缓存）
DbMetaFile(PATH_A).update(lambda d: {**d, "next": []})
chain_after_update = DbMetaFile(PATH_A).read()
# 原断言：assert DbMetaFile(PATH_A).read()["next"] == []
record("chain/next_cleared", chain_after_update["next"] == [], detail=chain_after_update)

healed = load_db(PATH_B)  # load_db 尾部调 _heal_next_edges
healed_uid = healed.get_uid()
# 原断言：assert healed.get_uid() == b_uid
record("chain/healed_uid_is_b", healed_uid == b_uid,
       detail={"healed": healed_uid, "expected": b_uid})

a_chain_after = DbMetaFile(PATH_A).read()
healed_edges = [e for e in a_chain_after["next"] if e["uid"] == b_uid]
# 原断言：assert len(healed_edges) == 1, f"A.next 必须被回填: {a_chain_after}"
record("chain/healed_edge_count", len(healed_edges), detail=a_chain_after)
healed_edge_path = healed_edges[0]["db_path"] if healed_edges else None
# 原断言：assert healed_edges[0]["db_path"] == PATH_B
record("chain/healed_edge_db_path", healed_edge_path)
INFO("[DONE] load_db heals missing upstream next edge")

db = healed  # 后续段落的操作句柄（load_db 返回的权威 Database）

# ── 2. backup 副本（写时 backup=True；backup_object 放弃语义）────────
# 注：db.backup_object 与源 write 同 task 同名会被 provenance 校验拒绝并撤销
# 写入段（零容忍数据安全语义）；带副本的正规写路径是 write_object(backup=True)。
master.launch_local_workers([{}])
# 保留：等待原语
assert wait_until(lambda: master.worker_count >= 1, timeout=30), "worker must connect"

import time
from fly import as_task


@as_task()
def write_with_backup(db):
    db.write_object("backed_up", {"payload": 41}, backup=True)


write_with_backup(healed)
t0 = time.time()
while len(master.completed_tasks) < 1:
    # 保留：等待循环体内的断言
    assert not master.failed_tasks, \
        f"write(backup=True) task failed: {master.get_task_error(master.failed_tasks[0])}"
    # 保留：等待循环体内的断言（超时守卫）
    assert time.time() - t0 < 60, "write task must finish"
    time.sleep(0.1)
backed_up_value = healed.read_object("backed_up")
# 原断言：assert healed.read_object("backed_up") == {"payload": 41}, "带副本写后源必须可读"
record("backup/backed_up_value", backed_up_value)

# backup_object 对不可见对象：尽力语义（ERR + 放弃），不抛
healed.backup_object("never_written_obj")
INFO("[DONE] write_object(backup=True) + backup_object abandon semantics")

# ── 3. var Python 对象分支往返 + miss 语义 ───────────────────────────
# var 是 Python 业务侧轻量对象 API（2026-09-02 裁定）：C++ 导出对象分支
# 已随裁定删除（原分支按 type_name 反查 _fly_storage 必败，见 fix 记录）。
db.set_var("py_obj", {"k": [1, 2]})
py_obj_value = db.get_var("py_obj")
# 原断言：assert db.get_var("py_obj") == {"k": [1, 2]}
record("var/py_obj_roundtrip", py_obj_value)
db.set_var("py_str", "hello_var")
py_str_value = db.get_var("py_str")
# 原断言：assert db.get_var("py_str") == "hello_var"
record("var/py_str_roundtrip", py_str_value)
never_set_value = db.get_var("never_set")
# 原断言：assert db.get_var("never_set") is None, "不存在的 var 应返回 None"
record("var/never_set_is_none", never_set_value is None, detail=never_set_value)
INFO("[DONE] set_var/get_var python-object roundtrip + miss returns None")

# ── 4. find_all_dbs 深链 ────────────────────────────────────────────
path_c = os.path.join(BASE, "c")
path_d = os.path.join(BASE, "d")
db_c = open_db(path_c, prev=[db])
db_d = open_db(path_d, prev=[db_c])

found = db_d.find_all_dbs()
uids = [x.get_uid() for x in found]
bfs_expected = [db_c.get_uid(), db.get_uid(), db_a.get_uid()]
# 原断言：assert uids == [db_c.get_uid(), db.get_uid(), db_a.get_uid()], \
record("find_all/bfs_order_is_c_b_a", uids == bfs_expected, detail=uids)

# 按条件过滤：只命中 A（logical_name；find_all_dbs 不含 uid 维度——仅 find_db 有）
by_name = db_d.find_all_dbs(logical_name="a")
by_name_uids = [x.get_uid() for x in by_name]
# 原断言：assert [x.get_uid() for x in by_name] == [db_a.get_uid()]
record("find_all/by_name_hits_a", by_name_uids == [db_a.get_uid()], detail=by_name_uids)
by_role = db_d.find_all_dbs(logical_name="c")
by_role_uids = [x.get_uid() for x in by_role]
# 原断言：assert [x.get_uid() for x in by_role] == [db_c.get_uid()]
record("find_all/by_name_hits_c", by_role_uids == [db_c.get_uid()], detail=by_role_uids)
# 无匹配 → 空
no_match = db_d.find_all_dbs(logical_name="nope")
# 原断言：assert db_d.find_all_dbs(logical_name="nope") == []
record("find_all/no_match_is_empty", no_match == [], detail=len(no_match))
INFO("[DONE] find_all_dbs deep chain BFS order + filtering")

master.stop()
INFO("[DONE] test_db_heal_backup")
