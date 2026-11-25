"""Project load 错误分支：无 _PROJECT_META → RuntimeError。

sub case（fly 进程跑），从原 test_project.py 的 test_load_project_errors 提取。
"""
import os, shutil
from _fly_log import INFO
from fly.project import Project
from test import record

empty = os.path.join(os.environ["FLY_CASE_LOG_DIR"], "project_empty")
if os.path.isdir(empty):
    shutil.rmtree(empty, ignore_errors=True)
os.makedirs(empty, exist_ok=True)

raised = None
try:
    Project.load(empty)
except RuntimeError as e:
    raised = {"type": type(e).__name__, "msg": str(e)}
# 原断言：assert raised, "Project.load on a dir without meta should raise RuntimeError"
# （原 raised 的值就是「消息含 _PROJECT_META」的布尔——拆成抛出判定 + 类型 +
#  消息关键词三条记录，语义等价）
record("load_without_meta_raises", raised is not None, detail=raised)
record("load_error_type", raised["type"] if raised else None)
record("load_error_mentions_meta",
       "_PROJECT_META" in raised["msg"] if raised else False, detail=raised)
INFO("[DONE] test_load_project_errors")
