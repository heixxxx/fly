"""测试用 Project 子类 + flow（独立模块，便于 pickle / 跨进程 import）。

供 test_project.py 及其 run 脚本共用。不放 test_project.py 顶层是为了避免
脚本被 exec 时类落在 __main__ 下导致 pickle 失败。

make_db flow 遵循异步范式（同 SolverProject.flows）：master 侧只做检查输入/建库/
提交入口 task/提交 freeze task 四件轻量事，提交后立即返回 db。freeze 作为 task，
依赖入口 task 写的数据完成，由 master 调度执行。
"""
from fly import as_task
from fly.project import Project, register_flow


class DemoProject(Project):
    """测试用的极简 Project 子类。"""

    pass


# ── 内部 task：写值（worker 执行，非阻塞）────────────────────────────
# 若显式传入 src_db，则从 src_db 读 "val" 写入 "from_src"（跨 db 间接依赖，
# 通过 kickoff task 的 inputs 依赖 src_db 的 val，master 调度推进）。

@as_task(inputs=lambda db, value, src_db:
         [src_db.get_full_name("val")] if src_db is not None else [])
def _write_val_task(db, value, src_db):
    db.write_object("val", value if value is not None else 42)
    if src_db is not None:
        db.write_object("from_src", src_db.read_object("val"))


# ── 内部 task：freeze（依赖上游数据写完，由 master 调度）─────────────

@as_task(inputs=lambda db, dep_keys: list(dep_keys))
def _demo_freeze_task(db, dep_keys):
    db.freeze()


# ── make_db flow（异步）──────────────────────────────────────────────

@register_flow(DemoProject)
def make_db(self, name: str, value=None, src_db=None):
    """模拟业务流程：建 name 库写 value；若显式传 src_db 则额外写 src 的数据。

    约定（同 SolverProject.flows）：name = db 子目录名 + 内部 key；
    跨流程数据依赖由用户显式传 db（src_db）。异步：提交入口 task + freeze task
    后立即返回 db。
    """
    db = self._create_db(name)

    # 提交入口 task：写 val（+ 可选从 src_db 读 from_src）。
    _write_val_task(db, value, src_db)

    # 提交 freeze task：inputs 依赖 val 写完，master 调度执行 freeze。
    _demo_freeze_task(db, self._freeze_task_deps(db, ["val"]))

    return db
