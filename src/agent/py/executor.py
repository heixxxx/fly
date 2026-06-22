"""Task executor for Worker Agent.

Provides the real execution logic that imports user modules, deserializes
arguments, and executes the original task functions.

Execution is split into three phases:
  - preprocess: prepare all task arguments (db creation/registration, remote
    data prefetch, var injection from the inlined TaskAssignMessage payloads).
  - execute:    call the resolved task function with the prepared arguments.
  - postprocess: hook for any post-execution work (currently a no-op, reserved
    for future use such as cleanup or metrics).
"""
import importlib
import pickle
import traceback

try:
    import cloudpickle
except ImportError:
    cloudpickle = None

try:
    from task.task import _USER_MODULE, _USER_FUNC_PREFIX
except ImportError:
    try:
        from task import _USER_MODULE, _USER_FUNC_PREFIX
    except ImportError:
        from fly.task import _USER_MODULE, _USER_FUNC_PREFIX

from _fly_agent import EXTaskExecResult, EXTaskExecStatus
from _fly_log import INFO, ERR

# db_id is a fixed-length (10 char) prefix of a full name "db_id:short_name".
# Used to split an inlined var's full name into the short name for local cache
# injection. Kept in sync with fly::db_id_len() (data_service.h).
_DB_ID_LEN = 10


def _split_full_name(full_name):
    """Split 'db_id:short_name' -> (db_id, short_name). db_id is fixed 10 chars."""
    if len(full_name) < _DB_ID_LEN + 2 or full_name[_DB_ID_LEN] != ':':
        return None, None
    return full_name[:_DB_ID_LEN], full_name[_DB_ID_LEN + 1:]


def _deserialize_args(args: list, worker) -> list:
    result = []
    for arg in args:
        if isinstance(arg, str) and arg.startswith("__fly_db__:"):
            parts = arg.split(":", 3)
            db_id = parts[1]
            if db_id not in worker._db_cache:
                base_path = parts[2] if len(parts) > 2 else ""
                data_path = parts[3] if len(parts) > 3 else ""
                from _fly_storage import ex_stg_get_data_service
                ds = ex_stg_get_data_service()
                if ds.has_database(db_id):
                    from _fly_storage import ex_stg_create_database_with_id
                    try:
                        from storage.database import _Database
                    except ImportError:
                        from database import _Database
                    db = _Database.__new__(_Database)
                    db._db = ex_stg_create_database_with_id(base_path, data_path, worker._worker_id, db_id)
                else:
                    try:
                        from storage.database import _Database
                    except ImportError:
                        from database import _Database
                    db = _Database(base_path, data_path, worker._worker_id)
                    db._db.set_db_id(db_id)
                worker._agent.register_database(db_id, db._db)
                worker._db_cache[db_id] = db
            result.append(worker._db_cache[db_id])
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
        if task_module == _USER_MODULE:
            if not task_name.startswith(_USER_FUNC_PREFIX):
                raise ValueError(
                    f"Worker received from_user task but task_name "
                    f"lacks serialized payload: {task_name!r}"
                )
            payload_hex = task_name[len(_USER_FUNC_PREFIX):]
            deserializer = cloudpickle if cloudpickle is not None else pickle
            return deserializer.loads(bytes.fromhex(payload_hex))
        try:
            from task.task import _task_registry
        except ImportError:
            try:
                from task import _task_registry
            except ImportError:
                from fly.task import _task_registry
        registered = _task_registry.get((task_module, task_name))
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
        deserialized_args = _deserialize_args(args, worker)

        # Inject inlined vars. Each VarPayload.var_name is a FULL name
        # (db_id:short_name); split to find the right Database and inject the
        # short name into its local cache.
        try:
            pending_vars = worker._agent.take_pending_task_vars()
        except Exception:
            pending_vars = []
        if pending_vars:
            from _fly_storage import FlyBuffer
            for vp in pending_vars:
                db_id, short_name = _split_full_name(vp.var_name)
                if db_id is None:
                    continue
                db_obj = worker._db_cache.get(db_id)
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

    def postprocess(task_id, result):
        """Phase 3: post-execution hook (currently a no-op).

        Reserved for future work such as per-task cleanup, metrics, or
        cache warming. Returning the result unchanged keeps the contract simple.
        """
        return result

    def executor(task_id: int, task_name: str, task_module: str, args: list) -> dict:
        result = {
            'task_id': task_id,
            'status': 0,
            'output': '',
            'error': '',
            'outputs': [],
            'frozen_dbs': [],
        }

        _frozen_before = set()

        try:
            # Phase 1: preprocess (db creation, var injection, etc.)
            deserialized_args = preprocess(task_id, task_name, task_module, args)

            for db_id, db_obj in worker._db_cache.items():
                if db_obj.is_frozen():
                    _frozen_before.add(db_id)

            # Phase 2: execute
            output = execute(task_id, task_name, task_module, deserialized_args)

            from _fly_storage import ex_stg_get_data_service
            ex_stg_get_data_service().drain_write_back()

            frozen_dbs = []
            for db_id, db_obj in worker._db_cache.items():
                if db_obj.is_frozen() and db_id not in _frozen_before:
                    frozen_dbs.append(db_id)

            # Phase 3: postprocess
            postprocess(task_id, output)

            result['status'] = 0
            result['output'] = str(output) if output is not None else ""
            result['frozen_dbs'] = frozen_dbs

        except Exception as e:
            result['status'] = 1
            result['output'] = ""
            result['error'] = traceback.format_exc()

            msg = f"Task execution failed: id={task_id}, name={task_name}, error={str(e)}"
            ERR(msg)

        return result

    return executor


__all__ = ['create_executor']
