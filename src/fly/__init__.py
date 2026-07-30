"""Fly — Distributed task execution framework.

Public API for creating databases, submitting tasks, and managing workers.

Example::

    import fly

    db = fly.open_db("./my_db")
    fly.launch_workers([{"role": "worker"}])

    @fly.as_task(inputs=lambda db: [])
    def my_task(db):
        ...

    fly.wait_tasks()
"""

import os

from _fly_log import WARN
import _fly_message as _msg

try:
    from storage.database import _Database
except ImportError:
    from storage.py.database import _Database

try:
    from core import get_config
except ImportError:
    from core.py import get_config

try:
    from core import get_work_directory
except ImportError:
    from core.py import get_work_directory

try:
    from task.task import as_task, task_name, wait_obj
except ImportError:
    from task.py.task import as_task, task_name, wait_obj

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


def load_project(path: str) -> 'Project':
    """Restore a Project from a previous run (master-only).

    Reads ``_PROJECT_META.json``, dynamically restores the real subclass
    (so registered flows are available), and fully ``load_db``-s every db.

    Args:
        path: Directory path of the existing project.

    Returns:
        A ``Project`` (or subclass) instance with all dbs restored.
    """
    return Project.load(path)


def open_db(path: str, data_path: str = "") -> '_Database':
    """Open a new database.

    If ``path`` already contains a database, auto-creates a numbered variant
    (``path.1``, ``path.2``, ...).

    Args:
        path: Directory path for the database.
        data_path: Optional separate path for data storage.

    Returns:
        A ``_Database`` instance.
    """
    actual_path = path
    n = 0
    while os.path.exists(os.path.join(actual_path, '_DB_META')):
        n += 1
        actual_path = f"{path}.{n}"
    if actual_path != path:
        WARN(f"open_db: path '{path}' already contains a database, "
             f"creating new database at '{actual_path}'")
    return _Database(actual_path, data_path)


def load_db(path: str) -> '_Database':
    """Load an existing database from a previous run.

    Must be called on the Master node.

    Args:
        path: Directory path of the existing database.

    Returns:
        A ``_Database`` instance.
    """
    return get_agent().load_db(path)


def merge_db(path: str, data_path: str = "", base_path: str = "",
             local_workers: int = 4, delete_source: bool = True) -> '_Database':
    """Merge a frozen database's data onto the master host.

    把分散在各源 host 本地 data_path 的 .dat 数据通过网络集中到 master host，
    产出一个 data 自包含、索引沿用共享 base_path 的合并数据库。

    Must be called on the Master node. Source db must be frozen (``db.freeze()``).

    Args:
        path: 源 db 的 base_path（共享存储，必须已 freeze）。
        data_path: 产物 data_path（master host 本地）。默认 ``path + ".merged_data"``。
        base_path: 产物 base_path。默认空=复用源 ``path``（idx 在共享盘，零搬迁）。
        local_workers: 仅当 master host **无**同 host worker 时拉起的 worker 数上限；
            已存在则不补齐，使用现有 worker 数作为并发度。
        delete_source: merge 全部成功后是否自动删源各 host 的原 .dat。

    Returns:
        合并后的 ``_Database`` 句柄。

    See ``docs/db-merge-design.md`` for design details.
    """
    return get_agent().merge_db(path, data_path, base_path, local_workers, delete_source)


def launch_workers(configs: list):
    """Launch local worker processes.

    Each config dict supports a ``'role'`` key.

    Args:
        configs: List of config dicts, one per worker.
    """
    get_agent().launch_local_workers(configs)


def wait_tasks(timeout: float = 30.0):
    """Block until all submitted tasks complete or timeout expires.

    Args:
        timeout: Maximum seconds to wait. Defaults to 30.

    Returns:
        True if all tasks completed, False if timed out.
    """
    return get_agent().wait_for_all_tasks(timeout=timeout)


def restart_failed_tasks(path: str):
    """Re-submit previously failed tasks from a persisted file.

    Args:
        path: Path to the persisted task failure file.
    """
    get_agent().restart_failed_tasks(path)


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
        并推送到 master（受 worker 本地 id/domain 两层配额控制，超限不推送但仍计数）。
      - master 进程：message 写本地 debug log + 直写 message.log + 输出 terminal
        （受 master 全局 id/domain 两层配额控制）。

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


def set_message_id_limit(limit: int):
    """设置全局 message id 配额（每个 id 独立计数，默认 20）。

    Args:
        limit: ``-1`` = 不限制；``0`` = 完全禁止；``N`` = 上限 N 次。
    """
    _msg.set_id_limit(limit)


def set_message_domain_limit(domain: str, limit: int):
    """设置某个 domain 的配额（该 domain 下所有 id 共享，默认 -1 不限制）。

    与 message id 配额同时生效，任一层超限即丢弃（但仍各计一次数）。

    Args:
        domain: domain 名（如 ``"SOLVER"``）。
        limit: ``-1`` = 不限制；``0`` = 完全禁止；``N`` = 上限 N 次。
    """
    _msg.set_domain_limit(domain, limit)


def set_master_print_id_limit(limit: int):
    """设置 master 进程的 message 打印配额（每个 id 独立，默认 20）。

    控制各 worker 推送来的 message 在 master 侧是否打印（message.log + terminal）。
    独立于各 worker 的触发计数配额（避免 summary 双算）。仅在 master 进程生效。

    Args:
        limit: ``-1`` = 不限制；``0`` = 完全禁止；``N`` = 上限 N 次。
    """
    _msg.set_master_print_id_limit(limit)


def set_master_print_domain_limit(domain: str, limit: int):
    """设置 master 进程某 domain 的打印配额（该 domain 下所有 id 共享，默认 -1 不限）。

    与 master id 打印配额同时生效，任一超限即丢弃。仅在 master 进程生效。

    Args:
        domain: domain 名（如 ``"SOLVER"``）。
        limit: ``-1`` = 不限制；``0`` = 完全禁止；``N`` = 上限 N 次。
    """
    _msg.set_master_print_domain_limit(domain, limit)


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


__all__ = [
    'open_db', 'load_db', 'merge_db', 'get_config', 'get_work_directory',
    'as_task', 'task_name', 'wait_obj',
    'launch_workers', 'wait_tasks',
    'restart_failed_tasks', 'get_task_error',
    'completed_tasks', 'pending_tasks', 'running_tasks', 'failed_tasks',
    'get_agent', 'MapReduceJob',
    'put_cache', 'get_cache', 'has_cache', 'remove_cache', 'clear_cache',
    'Project', 'register_flow', 'open_project', 'load_project',
    'UserDoc', 'Schema', 'document', 'help', 'register_module',
    'message', 'register_message_id', 'set_message_id_limit', 'set_message_domain_limit',
    'set_master_print_id_limit', 'set_master_print_domain_limit',
]


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
