"""Task executor for Worker Agent.

Provides the real execution logic that imports user modules, deserializes
arguments, and executes the original task functions.
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


def create_executor(worker) -> callable:
    

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
            if task_module == _USER_MODULE:
                if not task_name.startswith(_USER_FUNC_PREFIX):
                    raise ValueError(
                        f"Worker received from_user task but task_name "
                        f"lacks serialized payload: {task_name!r}"
                    )
                payload_hex = task_name[len(_USER_FUNC_PREFIX):]
                deserializer = cloudpickle if cloudpickle is not None else pickle
                original_func = deserializer.loads(bytes.fromhex(payload_hex))
            else:
                try:
                    from task.task import _task_registry
                except ImportError:
                    try:
                        from task import _task_registry
                    except ImportError:
                        from fly.task import _task_registry
                registered = _task_registry.get((task_module, task_name))
                if registered is not None:
                    original_func = getattr(registered, '_fly_original_func', registered)
                else:
                    module = importlib.import_module(task_module)
                    func = getattr(module, task_name)
                    original_func = getattr(func, '_fly_original_func', func)

            deserialized_args = _deserialize_args(args, worker)

            for db_id, db_obj in worker._db_cache.items():
                if db_obj.is_frozen():
                    _frozen_before.add(db_id)

            output = original_func(*deserialized_args)

            from _fly_storage import ex_stg_get_data_service
            ex_stg_get_data_service().drain_write_back()

            frozen_dbs = []
            for db_id, db_obj in worker._db_cache.items():
                if db_obj.is_frozen() and db_id not in _frozen_before:
                    frozen_dbs.append(db_id)

            result['status'] = 0
            result['output'] = str(output) if output is not None else ""
            result['frozen_dbs'] = frozen_dbs

            INFO(f"Task executed successfully: id={task_id}, name={task_name}, module={task_module}")

        except Exception as e:
            result['status'] = 1
            result['output'] = ""
            result['error'] = traceback.format_exc()

            msg = f"Task execution failed: id={task_id}, name={task_name}, error={str(e)}"
            ERR(msg)

        return result

    return executor


__all__ = ['create_executor', '_deserialize_args']
