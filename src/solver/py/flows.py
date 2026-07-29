"""SolverProject 的业务流程实现（独立模块）。

每个流程 API 通过 ``@register_flow(SolverProject)`` 注册到
:class:`solver.project.SolverProject`，实现"流程实现与类定义分离到不同模块"。

两个流程 API（详见 docs/project-design.md §4）：

- ``build_matrix(name, matrix_path)``：读 .npz 矩阵文件 → 矩阵存入 name 的 db → 返回 db
- ``solve(name, matrix_db, nsd, ...)``：读 matrix_db 的矩阵 → 求解 → 结果存入 name 的 db → 返回 db

**异步范式**（核心）：flow 内部用 @as_task 提交任务后**立即返回 db**，不做同步等待；
freeze 本身作为 task，通过 inputs 依赖数据写完，由 master 调度在就绪后执行并通知 master
更新 db frozen 状态。求解进度由 master 调度推进（ras_graph_coord 非阻塞 + check 自驱动
迭代链）。用户读结果时用 db.read_object（依赖图的 mark_data_ready 保证数据可见）或
wait_tasks 等待完成。
"""

import os

import numpy as np
from _fly_log import INFO

from fly import register_flow, as_task
from fly import UserDoc, Schema, document
from solver.project import SolverProject
from solver.ras_graph import solve_ras_graph as _solve_ras_graph  # noqa: F401 (legacy compat)
from solver.ras_graph import ras_graph_coord, _load_matrix


# ── 内部 task：写矩阵（worker 执行，非阻塞）──────────────────────────

@as_task()
def _write_matrix_task(db, matrix_dict):
    """把矩阵 dict 写入 db 的 "matrix" 对象。在 worker 上执行。"""
    db.write_object("matrix", matrix_dict)


# ── 内部 task：freeze（依赖上游数据写完，由 master 调度）─────────────

@as_task(inputs=lambda db, dep_keys: list(dep_keys))
def _freeze_db_task(db, dep_keys):
    """冻结 db。inputs 依赖上游对象写完后才被 master 调度。

    task 内 db.freeze() → 通知 master（stream 即时确认+广播；非 stream
    task 成功时 commit_pending_frozen + 广播）。
    """
    db.freeze()


# ── 内部 task：solve kickoff（依赖 matrix_db 的 matrix，worker 执行）──
#
# 这是异步 solve 的关键：通过 inputs 依赖 matrix_db 的 "matrix" 对象，
# master 在 matrix 写完后才调度本 task。本 task 在 worker 上读 matrix、还原
# 工作 npz、调 ras_graph_coord（coord 非阻塞，自驱动整个迭代链）。这样 solve
# flow 提交本 task 后即可立即返回 db，求解进度完全由 master 调度推进。

@as_task(inputs=lambda db, matrix_db, nsd, overlap_ratio, max_iter, tol, omega:
         [matrix_db.get_full_name("matrix")])
def _solve_kickoff_task(db, matrix_db, nsd, overlap_ratio,
                        max_iter, tol, omega):
    import os
    import numpy as np
    from solver.ras_graph import ras_graph_coord

    m = matrix_db.read_object("matrix")      # matrix_db 由闭包/参数传入
    work_npz = os.path.join(db.get_base_path(), "matrix.npz")
    np.savez(work_npz,
             n=np.int64(m["n"]), N=np.int64(m["N"]),
             rows=m["rows"], cols=m["cols"], vals=m["vals"], b=m["b"])
    ras_graph_coord(db, work_npz, nsd, overlap_ratio, max_iter, tol, omega)


# ── build_matrix：读文件 → 矩阵存入 db → 返回 db（异步）──────────────

build_matrix_doc = UserDoc("读 .npz 矩阵文件，存入 name 的 db，异步返回。")
build_matrix_doc.add_param("name",
    schema=Schema(str, check=lambda s: len(s) > 0, error="must not be empty"),
    required=True, desc="db 子目录名 + Project 内部 key")
build_matrix_doc.add_param("matrix_path",
    schema=Schema(str, check=lambda s: len(s) > 0, error="must not be empty"),
    required=True, desc=".npz 矩阵文件路径（COO 格式）")
build_matrix_doc.add_example("构建矩阵",
    code='''matrix_db = proj.build_matrix(name="matrix", matrix_path="poisson.npz")
proj.wait_frozen("matrix", timeout=60)''',
    desc="读 npz 文件建库，等待冻结后可用")
build_matrix_doc.add_keyword(["matrix", "npz", "solver", "input"])


@register_flow(SolverProject)
@document(build_matrix_doc)
def build_matrix(self, name: str, matrix_path: str):
    """流程 API 1：读 .npz 矩阵文件 → 矩阵存入 ``name`` 的 db → 返回该 db。

    异步：提交写 matrix 的 task + freeze task 后立即返回 db。matrix 写完 →
    master 调度 freeze task → db frozen 并通知 master。

    Args:
        self: 自动绑定的 SolverProject 实例。
        name: db 子目录名 + Project 内部 key。重名 → WARN + 自动递增。
        matrix_path: .npz 矩阵文件路径（COO 格式，见 ras_graph._load_matrix）。

    Returns:
        存矩阵数据的 ``_Database`` 句柄（freeze 异步进行中，可用 wait_frozen 等待）。
    """
    # ── Step 1: 检查输入（文件存在性，schema 无法覆盖）──
    import os as _os
    if not _os.path.isfile(matrix_path):
        raise ValueError(f"build_matrix: matrix file not found: {matrix_path}")

    # ── Step 2: 创建矩阵 db ──
    db = self._create_db(name)

    # ── Step 3: 提交入口 task（写 matrix，worker 执行，非阻塞）──
    m = _load_matrix(matrix_path)            # master 本地读文件（快）
    _write_matrix_task(db, m)

    # ── Step 4: 提交 freeze task（inputs 依赖 matrix 写完）──
    _freeze_db_task(db, self._freeze_task_deps(db, ["matrix"]))

    INFO(f"[SolverProject] build_matrix submitted: name={name}, N={m['N']}")
    return db


# ── solve：读 matrix_db 矩阵 → 求解 → 返回 db（异步）─────────────────

solve_doc = UserDoc("求解 matrix_db 中的矩阵，结果存入 name 的 db，异步返回。")
solve_doc.add_param("name",
    schema=Schema(str, check=lambda s: len(s) > 0, error="must not be empty"),
    required=True, desc="求解结果 db 的子目录名")
solve_doc.add_param("matrix_db",
    schema=Schema("_Database"),
    required=True, desc="含 read_object('matrix') 的数据源 db（显式传入）")
solve_doc.add_param("nsd",
    schema=Schema(int, check=lambda n: n >= 1, error="must be >= 1, got {value}"),
    required=True, desc="子域数（须有 >= nsd 个带 sd_i attributes 的 worker 在线）")
solve_doc.add_param("overlap_ratio",
    schema=Schema(float, check=lambda x: 0 <= x <= 1, error="must be in [0,1], got {value}"),
    required=False, default=0.50, desc="子域重叠比例")
solve_doc.add_param("max_iter",
    schema=Schema(int, check=lambda n: n >= 1, error="must be >= 1"),
    required=False, default=100, desc="最大迭代数")
solve_doc.add_param("tol",
    schema=Schema(float, check=lambda x: x >= 0, error="must be >= 0"),
    required=False, default=1e-8, desc="收敛阈值")
solve_doc.add_param("omega",
    schema=Schema.any_of(
        Schema((int, float), check=lambda w: 0 < w <= 2, error="number must be in (0,2]"),
        Schema(str, check=lambda w: w in ("coarse", "adaptive"),
               error="must be 'coarse' or 'adaptive'")),
    required=False, default=1.0, desc="松弛策略（数值或 'coarse'/'adaptive'）")
solve_doc.add_example("基础求解",
    code='''matrix_db = proj.build_matrix(name="matrix", matrix_path="poisson.npz")
result_db = proj.solve(name="solve", matrix_db=matrix_db, nsd=4)
proj.wait_frozen("solve", timeout=120)''',
    desc="单矩阵异步求解，返回 db 后等待冻结")
solve_doc.add_example("adaptive 松弛",
    code='''result_db = proj.solve(name="solve", matrix_db=matrix_db, nsd=4, omega="adaptive")''')
solve_doc.add_keyword(["ras", "solver", "linear", "iterative", "solve"])


@register_flow(SolverProject)
@document(solve_doc)
def solve(self, name: str, matrix_db, nsd,
          overlap_ratio=0.50, max_iter=100, tol=1e-8, omega=1.0):
    """流程 API 2：读 ``matrix_db`` 的矩阵 → 求解 → 结果存入 ``name`` 的 db → 返回该 db。

    异步：提交 kickoff task（inputs 依赖 matrix_db 的 matrix）+ freeze task（依赖
    __rasg__sol）后立即返回 db。matrix ready 后 master 调度 kickoff → coord 启动迭代链
    → assemble 写 __rasg__sol → master 调度 freeze task。整个求解进度由 master 调度推进，
    flow 不阻塞。

    ``matrix_db`` 由用户**显式传入**（build_matrix 产物或任意含 "matrix" 对象的外部 db）。

    **用户须预先唤起足够 worker**（带 ``sd_{i}`` attributes，数量 >= nsd）——flow 不负责
    worker 池管理（master 侧 flow 只做检查输入/建库/提交入口 task/提交 freeze task 四件
    轻量事）。参考原 solver 测试的 worker 唤起方式。

    Args:
        self: 自动绑定的 SolverProject 实例。
        name: 求解结果 db 的子目录名 + 内部 key。
        matrix_db: **显式传入**的数据源 db（含 read_object("matrix")）。
        nsd: 子域数（须有 >= nsd 个带 sd_i attributes 的 worker 在线）。
        overlap_ratio: 重叠比例（默认 0.50）。
        max_iter: 最大迭代数（默认 100）。
        tol: 收敛阈值（默认 1e-8）。
        omega: 松弛策略（1.0 / "coarse" / "adaptive"）。

    Returns:
        存求解过程的 ``_Database`` 句柄（求解异步进行中；__rasg__sol 就绪后可读结果，
        可用 wait_frozen 等待整库 frozen）。
    """
    # ── Step 1: 创建求解 db ──
    # （nsd>=1 / matrix_db 非 None 已由 @document 的 schema 校验覆盖）
    db = self._create_db(name)

    # ── Step 2: 提交入口 task（kickoff：依赖 matrix_db 的 matrix）──
    # master 在 matrix ready 后调度 kickoff；kickoff 在 worker 上读 matrix、还原 npz、
    # 调 ras_graph_coord（coord 非阻塞，提交第一轮 compute/check 后返回，check 在 worker
    # 内提交下一轮，整个迭代链由 master 调度自驱动）。
    _solve_kickoff_task(db, matrix_db, nsd, overlap_ratio, max_iter, tol, omega)

    # ── Step 3: 提交 freeze task（依赖求解完成标记 __rasg__sol）──
    _freeze_db_task(db, self._freeze_task_deps(db, ["__rasg__sol"]))

    INFO(f"[SolverProject] solve submitted: name={name}, nsd={nsd}, omega={omega}")
    return db
