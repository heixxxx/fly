"""Fly — Distributed task execution framework.

Public API for creating databases, submitting tasks, and managing workers.

Example::

    import fly

    db = fly.open_db("./my_db")
    fly.launch_workers([{}, {"role": "storage_only"}])

    @fly.as_task(inputs=lambda db: [])
    def my_task(db):
        ...

    fly.wait_tasks()
"""

import os

from _fly_log import WARN, INFO
import _fly_message as _msg

from storage import Database
from storage import generate_uid, make_edge
from core import get_config, get_work_directory
from task import as_task, task_name, wait_obj
from monitor import launch_monitor_gui

from fly.runtime import get_agent
from fly.userdoc import UserDoc, Schema, document, help, register_module
from fly.mapreduce import MapReduceJob
from fly.project import Project, register_flow


def open_project(path: str) -> 'Project':
    """Create or bind a Project at ``path``.

    Returns a base ``Project`` instance (mechanism shell, no business flows).
    To use flows, construct a concrete subclass (e.g. ``SolverProject(path)``)
    or restore one via :func:`load_project`.

    Args:
        path: Directory path for the project (created if absent).

    Returns:
        A ``Project`` instance.
    """
    return Project(path)


def load_project(path: str, resume: bool = False) -> 'Project':
    """Restore a Project from a previous run (master-only).

    Reads ``_PROJECT_META.json``, dynamically restores the real subclass
    (so registered flows are available), and fully ``load_db``-s every db
    (含 temp 落盘恢复——已完成 task 的 temp 输出跨进程 ready).

    Args:
        path: Directory path of the existing project.
        resume: 若 True，恢复完成后自动断点重跑（重投 ``{project}/failed_tasks.bin``
            里的 FAILED/PENDING/RUNNING task；bin 不存在为 no-op）。前提：worker
            已唤起。

    Returns:
        A ``Project`` (or subclass) instance with all dbs restored.
    """
    proj = Project.load(path)
    if resume:
        proj.resume()
    return proj


def migrate_project(path: str, new_path: str = "", *, consolidate: bool = False,
                    local_workers: int = 4) -> 'Project':
    """Project 整体迁移（master-only，全程无超时——数据量与集群 IO 不可预估）。

    两种模式（可组合）：

    - **数据集中**（``consolidate=True``，在线）：把分散在各源 host 本地盘的
      .dat 数据经网络集中到 master host（逐 db ``merge_db``，产物 data 在
      ``{db_path}.merged_data``，位于 project 目录内，自包含）。要求全部 db
      已 frozen（merge 前提）——未 frozen 报错列出；半成品 project 跨主机
      不支持（先跑完或单机搬迁）。
    - **目录搬迁**（``new_path`` 非空，离线）：project 目录整体挪到 new_path
      （含全部 db + failed_tasks.bin，断点能力随迁），meta db_path/data_path
      改写 + _DB_CHAIN 邻居边更新（uid 不变）。不要求 frozen——半成品 db 靠
      idx 事务段保证搬后可恢复。前提：相关进程已退出。

    Args:
        path: 现有 project 目录。
        new_path: 迁移目标目录（空 = 只 consolidate 不搬迁）。
        consolidate: 先跨主机集中数据再搬迁。
        local_workers: consolidate 时 master host 无同 host worker 的拉起上限。

    Returns:
        迁移后的 ``Project`` 实例（绑定新路径）。
    """
    from fly.project import Project
    proj = Project(path)

    if consolidate:
        agent = get_agent()
        unfrozen = [info["db_path"] for info in proj._meta["dbs"].values()
                    if not agent._agent.is_db_frozen(info["db_path"])]
        if unfrozen:
            raise RuntimeError(
                f"migrate_project(consolidate=True): {len(unfrozen)} db(s) not "
                f"frozen (merge precondition): {unfrozen} — finish or freeze "
                f"them first, or migrate without consolidate on a single host")
        for actual, info in proj._meta["dbs"].items():
            INFO(f"migrate_project: consolidating db '{actual}' "
                 f"({info['db_path']})")
            # 跨进程场景先恢复源对象索引（master remote_idx + 源 host worker
            # local_idx）——merge task 的跨机拉源依赖对象位置已知。
            load_db(info["db_path"])
            merge_db(info["db_path"], local_workers=local_workers)

    if new_path:
        proj.migrate(new_path)

    return proj


def open_db(path: str, data_path: str = "", db_cls=None, prev=None,
            logical_name=None) -> 'Database':
    """Open a new database.

    If ``path`` already contains a database, auto-creates a numbered variant
    (``path.1``, ``path.2``, ...).

    Args:
        path: Directory path for the database.
        data_path: Optional separate path for data storage.
        db_cls: Database 子类（决定 role）。默认 Database（无 role）。
        prev: 前驱 db 句柄列表（DAG 边）。默认 None（无前驱）。
        logical_name: db 的逻辑名（用于 _DB_CHAIN）。默认取 path basename。

    Returns:
        A ``Database`` instance.
    """
    actual_path = path
    n = 0
    while os.path.exists(os.path.join(actual_path, '_DB_META')):
        n += 1
        actual_path = f"{path}.{n}"
    if actual_path != path:
        WARN(f"open_db: path '{path}' already contains a database, "
             f"creating new database at '{actual_path}'")

    cls = db_cls or Database
    db = cls(actual_path, data_path)

    # 初始化 _DB_CHAIN
    role = cls.role if cls.role is not None else None
    uid = generate_uid(actual_path, role)
    lname = logical_name or os.path.basename(actual_path)

    # 构造前驱边
    prev_edges = []
    if prev:
        for prev_db in prev:
            prev_uid = prev_db.get_uid()
            prev_role = prev_db.get_role()
            prev_lname = prev_db._chain_logical_name or os.path.basename(prev_db.get_db_path())
            prev_edges.append(make_edge(prev_uid, prev_role, prev_lname, prev_db.get_db_path()))

    db._init_chain(uid, role, lname, prev_edges, data_path=data_path)

    # 运行时 uid 索引上报（master-only）：restart 解析 bin 记录 db 引用的
    # 跨路径稳定键（worker 进程无 master agent，静默跳过）。
    try:
        from fly.runtime import _mode
        if _mode == "master":
            from fly.runtime import get_agent
            get_agent()._agent.register_db_uid(uid, actual_path)
    except Exception:
        pass

    # 回填前驱的 next（双向链）
    if prev:
        self_edge = make_edge(uid, role, lname, actual_path)
        for prev_db in prev:
            prev_db._add_next_to_chain(self_edge)

    return db


def load_db(path: str) -> 'Database':
    """Load an existing database from a previous run.

    Must be called on the Master node.

    Args:
        path: Directory path of the existing database.

    Returns:
        A ``Database`` instance.
    """
    return get_agent().load_db(path)


def merge_db(path: str, data_path: str = "", merge_db_path: str = "",
             local_workers: int = 4, delete_source: bool = True) -> 'Database':
    """Merge a frozen database's data onto the master host.

    把分散在各源 host 本地 data_path 的 .dat 数据通过网络集中到 master host，
    产出一个 data 自包含、索引沿用共享 db_path 的合并数据库。

    Must be called on the Master node. Source db must be frozen (``db.freeze()``).
    **全程无超时**：merge 数据量与集群 IO 速度不可预估（EDA 数 T 级 db 常见），
    等待只被显式失败信号终结（task 失败 / worker 判死联动）。

    Raises:
        RuntimeError: 任一对象 merge 失败（源数据保留支撑重试，失败产物已清理）。

    Args:
        path: 源 db 的 db_path（共享存储，必须已 freeze）。
        data_path: 产物 data_path（master host 本地）。默认 ``path + ".merged_data"``。
        db_path: 产物 db_path。默认空=复用源 ``path``（idx 在共享盘，零搬迁）。
        local_workers: 仅当 master host **无**同 host worker 时拉起的 worker 数上限；
            已存在则不补齐，使用现有 worker 数作为并发度。
        delete_source: merge 全部成功后是否自动删源各 host 的原 .dat。

    Returns:
        合并后的 ``Database`` 句柄。

    See ``docs/db-merge-design.md`` for design details.
    """
    return get_agent().merge_db(path, data_path, merge_db_path,
                                local_workers, delete_source)


def launch_workers(configs: list):
    """Launch local worker processes.

    Each config dict supports:

    - ``'role'``: worker 静态身份——``"hybrid"``（默认，任务执行+数据存储）/
      ``"storage_only"``（存储 worker：不参与计算 task 调度，仍可执行内部数据
      搬运 task 与作为数据副本持有者）。注册时设定，不可变更；独立于 attributes。
    - ``'attributes'``: 可随时增减的能力标签（参与调度匹配）。
    - ``'host'``: hostname override（单机多 host 测试）。

    Args:
        configs: List of config dicts, one per worker.
    """
    get_agent().launch_local_workers(configs)


def ensure_workers(workers, timeout: float = 10.0, exclude: str = None) -> bool:
    """向 master 申请现有 worker 并为选中 worker 追加指定属性（不启动新进程）。

    workers: list，长度即申请的 worker 数；每个元素是该 worker 要追加的属性
    集合——str（单属性简写）或 str 的 list。属性是追加去重，已有属性保留。

    收集语义（两阶段）：时限内只收空闲候选；到点仍未齐则放宽到忙碌候选
    （打上属性后不等其空闲，后续 task 由调度系统按 requires 自动派发）。
    排除后的全量池（IDLE+BUSY）本身盖不住申请数时立即抛 RuntimeError，
    不等待。幂等：重复调用同规格不重复分配。

    典型工作流：求解正式启动前确认编队，再启动::

        ensure_workers([
            db.worker_attr("sd_0"),
            db.worker_attr("check"),
        ], timeout=10.0, exclude=r"^rasg:")

    Args:
        workers: 属性集合 list（见上）。
        timeout: 空闲收集时限秒数；<=0 跳过阶段一直接按放宽口径收集。
        exclude: 正则字符串（re.search）；worker 任一既有属性命中即排除出
            候选池。并发 flow 场景用它排除已被其他 flow 编队的 worker（配合
            db.worker_attr 的 rasg:{uid}: 命名空间——单点定义在
            storage.Database 基类）。

    Returns:
        True（编队就绪）。资源不足或生效超时抛 RuntimeError。

    See Also:
        Master.ensure_workers — 完整契约（追加语义、静态预检、生命周期）。
    """
    return get_agent().ensure_workers(workers, timeout=timeout, exclude=exclude)


def wait_tasks(timeout: float = 30.0):
    """Block until all submitted tasks complete or timeout expires.

    Args:
        timeout: Maximum seconds to wait. Defaults to 30.

    Returns:
        True if all tasks completed, False if timed out.
    """
    return get_agent().wait_for_all_tasks(timeout=timeout)


def wait_workers_registered(timeout: float = None) -> bool:
    """Block until every launched worker has registered with the master.

    Designed for slow schedulers (bsub/LSF): after launching, workers may take
    minutes to actually start. This API makes no default assumption about how
    long registration takes.

    Args:
        timeout: Maximum seconds to wait. None = use config
            ``worker_register_timeout`` (default 0 = wait indefinitely).

    Returns:
        True if all expected workers registered, False if timed out.
    """
    return get_agent().wait_workers_registered(timeout=timeout)


def expect_workers(worker_ids):
    """Register expected-worker placeholders for externally launched workers.

    ``launch_workers`` registers placeholders automatically. Use this API when
    launching workers yourself (e.g. via bsub/LSF running ``fly --worker``) so
    that ``wait_workers_registered`` can wait for them.

    Args:
        worker_ids: Iterable of worker ids that will be launched.
    """
    get_agent().expect_workers(worker_ids)


def restart_failed_tasks(dbs) -> int:
    """Re-submit previously failed tasks, discovered by db ownership.

    每个 task 的失败记录按其归属 db 落盘（{owner_db_path}/failed_tasks.bin，
    见 DEVELOPMENT_GUIDELINES "Task db 归属规则"节）。本函数在给定 db 的目录
    下自动搜索 failed_tasks.bin 并重投（读即删；重投后再失败会重新落盘）。

    Args:
        dbs: db 对象 / db_path 字符串 / 二者混合的 list（单元素亦可）。
            无归属 task 的 fallback bin 在 {log_dir} 下——传 log_dir 目录
            路径字符串即可找回。

    Returns:
        重投的 task 总数。
    """
    return get_agent().restart_failed_tasks(dbs)


def get_task_error(task_id: int) -> str:
    """Get the error message for a failed task.

    Args:
        task_id: The ID of the failed task.

    Returns:
        Error message string.
    """
    return get_agent().get_task_error(task_id)


def put_cache(key: str, value):
    """Store a Python object in the local agent cache.

    The cache lives for the lifetime of the agent process (Master or Worker)
    and is strictly local — not shared across workers.  Useful for passing
    data between tasks on the same worker without network/disk I/O.

    Args:
        key: String key for the cached value.
        value: Any Python object.
    """
    get_agent().put_cache(key, value)


def get_cache(key: str, default=None):
    """Retrieve a cached Python object by key.

    Args:
        key: String key that was used with :func:`put_cache`.
        default: Value to return if *key* is not found.

    Returns:
        The cached Python object, or *default* if not found.
    """
    return get_agent().get_cache(key, default)


def has_cache(key: str) -> bool:
    """Return ``True`` if *key* exists in the local agent cache."""
    return get_agent().has_cache(key)


def remove_cache(key: str):
    """Remove a single entry from the local agent cache.

    Raises:
        KeyError: If *key* is not in the cache.
    """
    get_agent().remove_cache(key)


def clear_cache():
    """Remove all entries from the local agent cache."""
    get_agent().clear_cache()


# =============================================================================
# Message 日志系统 — 高价值信息的远程推送与集中收集。
# =============================================================================
#
# message 的级别由 id 决定：注册时绑定级别（INFO/WARN/ERROR），发送时不传级别。
# 见 register_message_id / message。


def message(domain_id: str, source: int, msg: str):
    """发送一条高价值 message。

    message id 格式 ``"DOMAIN::NNNN"``（domain 大写，id 4 位补零，如 ``"SOLVER::0047"``）。
    仅注册过的 domain_id 才会被打印/发送（见 :func:`register_message_id`）。
    **级别由 id 决定**（注册时绑定），本接口不接收 level 参数。

    行为分进程：
      - worker 进程：message 写本地 debug log（带 ``[DOMAIN::NNNN] <source>`` 前缀），
        并推送到 master（受 worker 本地三层配额控制，超限不推送但仍计数）。
      - master 进程：message 写本地 debug log + 直写 message.log + 输出 terminal
        （受 master 打印三层配额控制）。

    Args:
        domain_id: message id，如 ``"SOLVER::0047"``。未注册则丢弃。
        source: 触发位置标识（int，业务自定义）。打印为 ``[DOMAIN::NNNN] <source> msg``，
            用于同一 id 在不同位置触发时快速定位。不参与配额。
        msg: message 文本。
    """
    _msg.send_message(domain_id, source, msg)


def register_message_id(domain_id: str, level: str = "INFO"):
    """注册一个合法 message id 进白名单并绑定其级别。

    各模块在 Python 模块初始化时调用，注册自己 domain 下的合法 id。
    只有注册过的 id 才能被 :func:`message` 打印/发送。**级别在此绑定**，
    发送时按 id 查级别，调用点不再传级别。

    Args:
        domain_id: message id，如 ``"SOLVER::0047"``。
        level: ``"INFO"`` / ``"WARN"`` / ``"ERROR"``（默认 ``"INFO"``）。
    """
    _msg.register_message_id(domain_id, level)


def set_message_global_limit(limit: int):
    """设置全局默认 message 配额（兜底，默认 20）。

    仅对未显式设置 per-id / per-domain 配额的 id 生效。配额优先级链：
    **per-id > per-domain > global**，仅取第一个显式设置的层级（详见 :func:`set_message_id_limit`）。

    单一 limit 同时控制两处（用户无需分别设置）：
      - **worker 发送配额**：每 worker 每 id 最多输出 limit 条（源头控流量）。
      - **master 打印配额**：master 汇聚打印总量 limit 条（master 总量限流）。
    master 进程调用后会自动广播给所有 worker（支持运行时动态修改配额）。

    Args:
        limit: ``-1`` = 不限制；``0`` = 完全禁止；``N`` = 上限 N 次。
    """
    _msg.set_global_limit(limit)


def set_message_id_limit(domain_id: str, limit: int):
    """设置单个 message id 的独立配额（per-id，覆盖 global 与 domain）。

    配额优先级链（链式，仅第一个显式设置的层级生效，其余层完全不检查）：

      1. **per-id**（最细）：本接口为单个 id 设独立配额。设了 per-id 的 id 只看这一层，
         domain / global 都不检查。
      2. **per-domain**（见 :func:`set_message_domain_limit`）：未设 per-id 时，
         看该 id 所属 domain 的配额。
      3. **global**（见 :func:`set_message_global_limit`，默认 20）：未设 per-id 且未设
         per-domain 时，用全局默认兜底。global 永远有值，但不因为「有值」就屏蔽上层。

    单一 limit 同时控制 worker 发送配额 + master 打印配额（详见 :func:`set_message_global_limit`），
    并自动同步给所有 worker。

    Args:
        domain_id: message id，如 ``"SOLVER::0047"``。
        limit: ``-1`` = 不限制；``0`` = 完全禁止；``N`` = 上限 N 次。
    """
    _msg.set_id_limit(domain_id, limit)


def set_message_domain_limit(domain: str, limit: int):
    """设置某个 domain 的配额（per-domain，覆盖 global）。

    该 domain 下所有未设 per-id 配额的 id 各自独立计数（语义同 global，每 id 独立），
    仅对该 domain 内 id 生效。未显式设置的 domain 下沉到 global。
    优先级链 per-id > per-domain > global，详见 :func:`set_message_id_limit`。

    单一 limit 同时控制 worker 发送配额 + master 打印配额（详见 :func:`set_message_global_limit`），
    并自动同步给所有 worker。

    Args:
        domain: domain 名（如 ``"SOLVER"``）。
        limit: ``-1`` = 不限制；``0`` = 完全禁止；``N`` = 上限 N 次。
    """
    _msg.set_domain_limit(domain, limit)


def __getattr__(name):
    if name == "completed_tasks":
        return get_agent().completed_tasks
    if name == "pending_tasks":
        return get_agent().pending_tasks
    if name == "running_tasks":
        return get_agent().running_tasks
    if name == "failed_tasks":
        return get_agent().failed_tasks
    if name == "port":
        return get_agent().port
    raise AttributeError(f"module 'fly' has no attribute {name}")


def get_fly_binary() -> str:
    """Get the path to the fly binary.

    Returns the wrapper script (build/bin/fly) when available, since it sets
    FLY_BUILD and LD_LIBRARY_PATH — preferred over the raw fly.bin for spawning
    subprocesses.

    Resolution order (first hit wins):
      1. ``sys._fly_binary`` — injected by the C++ launcher (main.cpp), which
         knows the exact layout at startup. Always correct when running under
         fly.
      2. ``FLY_BUILD`` env var — set by the wrapper and runqa.
      3. Path inference from this module's location (build or source layout).
      4. ``fly`` on PATH.
    """
    import os
    import sys

    # 1. Injected by the launcher — authoritative.
    injected = getattr(sys, "_fly_binary", None)
    if injected and os.path.isfile(injected) and os.access(injected, os.X_OK):
        return injected

    # 2. FLY_BUILD env (wrapper / runqa).
    fly_build = os.environ.get("FLY_BUILD")
    if fly_build:
        candidate = os.path.join(fly_build, "bin", "fly")
        if os.path.isfile(candidate) and os.access(candidate, os.X_OK):
            return candidate

    # 3. Inference from this file's location. Installed build layout puts this
    # module at <root>/build/python/fly/__init__.py (root is 3 levels up);
    # source layout puts it at <root>/src/fly/__init__.py (2 levels up).
    this_dir = os.path.dirname(os.path.abspath(__file__))
    for project_root in (
        os.path.dirname(os.path.dirname(os.path.dirname(this_dir))),  # build/python/fly → root
        os.path.dirname(os.path.dirname(this_dir)),                   # src/fly → root
    ):
        candidate = os.path.join(project_root, "build", "bin", "fly")
        if os.path.isfile(candidate) and os.access(candidate, os.X_OK):
            return candidate

    # 4. PATH fallback.
    import shutil
    fly_on_path = shutil.which("fly")
    if fly_on_path:
        return fly_on_path
    raise RuntimeError("Cannot find fly binary")
