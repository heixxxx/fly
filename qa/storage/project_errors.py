"""Project load 错误分支：无 _PROJECT_META → RuntimeError。

sub case（fly 进程跑），从原 test_project.py 的 test_load_project_errors 提取。
"""
import os, shutil
from _fly_log import INFO
from fly.project import Project

empty = os.path.join(os.environ["FLY_CASE_LOG_DIR"], "project_empty")
if os.path.isdir(empty):
    shutil.rmtree(empty, ignore_errors=True)
os.makedirs(empty, exist_ok=True)

raised = False
try:
    Project.load(empty)
except RuntimeError as e:
    raised = "_PROJECT_META" in str(e)
assert raised, "Project.load on a dir without meta should raise RuntimeError"
INFO("[PASS] test_load_project_errors")
