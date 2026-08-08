"""Task executor for Worker Agent.

Provides the real execution logic that imports user modules, deserializes
arguments, and executes the original task functions.

Execution is split into three phases:
  - preprocess: prepare all task arguments (db creation/registration, remote
    data prefetch, var injection from the inlined TaskAssignMessage payloads).
  - execute:    call the resolved task function with the prepared arguments.
  - postprocess: drain the write-back queue so every write issued during
    execute is flushed to disk before the C++ layer collects tracked writes.

Freeze handling: freeze is an active behavior within a task — Database::freeze()
fires an immediate DatabaseFreezeNotification to master (which registers it as
"pending" in non-stream mode). No before/after snapshot diffing is done here;
master commits pending freezes by task_id on task completion.
"""
import importlib
import pickle
import traceback

try:
    import cloudpickle
except ImportError:
    cloudpickle = None

from task import USER_MODULE, USER_FUNC_PREFIX

from _fly_agent import EXTaskExecResult, EXTaskExecStatus
from _fly_log import INFO, ERR

from storage import Database, DbChainFile
from storage import get_registry as get_chain_registry

# db_path（db_path）是 full_name "db_path:short_name" 的前缀（变长，含 '/'）。
# split 用 rfind(':') —— short_name 是逻辑对象名不含 ':'，最后一个 ':' 必是分隔符。
# 与 C++ data_service.h 的 split_full_name 保持一致。


def _split_full_name(full_name):
    """Split 'db_path:short_name' -> (db_path, short_name). Uses rfind(':')."""
    pos = full_name.rfind(':')
    if pos < 0:
        return None, None
    return full_name[:pos], full_name[pos + 1:]


def deserialize_args(args: list, worker) -> list:
    result = []
    for arg in args:
        if isinstance(arg, str) and arg.startswith("__fly_db__:"):
            # 支持两种格式：
            #   新格式（db chain）：__fly_db__:{uid}:{db_path}:{data_path}
            #   旧格式：__fly_db__:{db_path}:{data_path}
            parts = arg.split(":", 3)  # maxsplit=3 防止 data_path 含 ':' 被过度拆分
            uid = None
            db_path = ""
            data_path = ""
            if len(parts) == 4:
                uid = parts[1]
                db_path = parts[2]
                data_path = parts[3]
            elif len(parts) == 3:
                db_path = parts[1]
                data_path = parts[2]
            else:
                db_path = parts[1] if len(parts) > 1 else ""
                data_path = parts[2] if len(parts) > 2 else ""

            cache_key = uid or db_path
            if cache_key not in worker._db_cache:
                from _fly_storage import ex_stg_get_data_service
                ds = ex_stg_get_data_service()

                # 读 _DB_CHAIN 一次（role + chain info），失败时安全 fallback 到基类。
                chain_data = None
                try:
                    chain_data = DbChainFile(db_path).read()
                except Exception:
                    pass

                # 按 role 选子类
                role = chain_data.get("role") if chain_data else None
                cls = Database._ROLE_REGISTRY.get(role, Database) if role else Database

                if ds.has_database(db_path):
                    from _fly_storage import ex_stg_create_database_with_path
                    db = cls.__new__(cls)
                    db._db = ex_stg_create_database_with_path(db_path, data_path, worker._worker_id, db_path)
                else:
                    db = cls(db_path, data_path, worker._worker_id)

                # 从已读的 chain_data 恢复链信息（不重复读文件）
                db._chain_file = DbChainFile(db_path)
                db._chain_uid = chain_data.get("uid") if chain_data else None
                db._chain_role = role
                db._chain_logical_name = chain_data.get("logical_name") if chain_data else None
                if db._chain_uid:
                    get_chain_registry().register(db._chain_uid, db.get_db_path())

                worker._agent.register_database(db_path, db._db)
                worker._db_cache[cache_key] = db
                if uid:
                    worker._db_cache[db_path] = db
            result.append(worker._db_cache[cache_key])
        else:
            result.append(pickle.loads(bytes.fromhex(arg)))
    return result


def create_executor(worker):
    """Create an executor bound to `worker`.

    The executor runs each task in three phases:
      preprocess -> execute -> postprocess.
    """

    def _resolve_func(task_name, task_module):
        """Resolve the original task function from its name/module."""
        if task_module == USER_MODULE:
            if not task_name.startswith(USER_FUNC_PREFIX):
                raise ValueError(
                    f"Worker received from_user task but task_name "
                    f"lacks serialized payload: {task_name!r}"
                )
            payload_hex = task_name[len(USER_FUNC_PREFIX):]
            deserializer = cloudpickle if cloudpickle is not None else pickle
            return deserializer.loads(bytes.fromhex(payload_hex))
        from task import task_registry
        registered = task_registry.get((task_module, task_name))
        if registered is not None:
            return getattr(registered, '_fly_original_func', registered)
        module = importlib.import_module(task_module)
        func = getattr(module, task_name)
        return getattr(func, '_fly_original_func', func)

    def preprocess(task_id, task_name, task_module, args):
        """Phase 1: prepare all task arguments.

        - Deserialize args (creates/registers Database objects).
        - Inject master-inlined vars (from TaskAssignMessage) into the relevant
          Database local caches so get_var hits locally during execute.
        Returns the deserialized argument list.
        """
        deserialized_args = deserialize_args(args, worker)

        # Inject inlined vars. Each VarPayload.var_name is a FULL name
        # (db_path:short_name); split to find the right Database and inject the
        # short name into its local cache.
        try:
            pending_vars = worker._agent.take_pending_task_vars()
        except Exception:
            pending_vars = []
        if pending_vars:
            from _fly_storage import FlyBuffer
            for vp in pending_vars:
                db_path, short_name = _split_full_name(vp.var_name)
                if db_path is None:
                    continue
                db_obj = worker._db_cache.get(db_path)
                if db_obj is not None:
                    # vp.value is raw bytes (from the wire); wrap into a FlyBuffer
                    # for _inject_var (which takes FlyBufferPtr, zero-copy in C++).
                    buf = FlyBuffer()
                    buf.write(vp.value)
                    db_obj._db._inject_var(short_name, buf, vp.type_name)

        return deserialized_args

    def execute(task_id, task_name, task_module, deserialized_args):
        """Phase 2: call the resolved task function with prepared arguments."""
        original_func = _resolve_func(task_name, task_module)
        return original_func(*deserialized_args)

    def postprocess(task_id):
        """Phase 3: post-execution cleanup.

        Drains the write-back queue so that every write_object issued during
        execute has been flushed to disk and its record_write callback fired
        (populating the C++ current_writes_ list that end_task collects). This
        must run only on the success path — on failure, dirty writes are rolled
        back by the C++ cleanup_failed_task_writes, so draining would be wrong.
        """
        from _fly_storage import ex_stg_get_data_service
        ex_stg_get_data_service().drain_write_back()

    def executor(task_id: int, task_name: str, task_module: str, args: list) -> dict:
        result = {
            'task_id': task_id,
            'status': 0,
            'output': '',
            'error': '',
            'outputs': [],
            # frozen_dbs 保留以兼容消息结构，但 master 不再依赖它做 commit：
            # freeze 是 task 内主动行为，发生时已通过 DatabaseFreezeNotification 即时
            # 通知 master 登记 pending；task 完成时 master 按 task_id 从 pending 迁移。
            # 旧的"遍历 _db_cache 拍前后快照算差集"已删除（不可靠且冗余）。
            'frozen_dbs': [],
        }

        try:
            # Phase 1: preprocess (db creation, var injection, etc.)
            deserialized_args = preprocess(task_id, task_name, task_module, args)

            # Phase 2: execute
            output = execute(task_id, task_name, task_module, deserialized_args)

            # Phase 3: postprocess (drain write-back so writes are flushed &
            # recorded before C++ end_task collects them)
            postprocess(task_id)

            result['status'] = 0
            result['output'] = str(output) if output is not None else ""

        except Exception as e:
            result['status'] = 1
            result['output'] = ""
            result['error'] = traceback.format_exc()

            msg = f"Task execution failed: id={task_id}, name={task_name}, error={str(e)}"
            ERR(msg)

        return result

    return executor


