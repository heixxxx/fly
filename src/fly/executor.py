"""Task executor for Worker Agent.

Provides the real execution logic that imports user modules, deserializes
arguments, and executes the original task functions.
"""
import importlib
import pickle
import logging
import traceback

from _fly_agent import EXTaskExecResult, EXTaskExecStatus

logger = logging.getLogger("fly")


def _deserialize_args(args: list, worker) -> list:
    result = []
    for arg in args:
        if isinstance(arg, str) and arg.startswith("__fly_db__:"):
            parts = arg.split(":", 3)
            db_id = parts[1]
            if db_id not in worker._db_cache:
                base_path = parts[2] if len(parts) > 2 else ""
                data_path = parts[3] if len(parts) > 3 else ""
                from fly.database import _Database
                db = _Database(base_path, data_path, worker._worker_id)
                worker._db_cache[db_id] = db
                worker._agent.register_database(db_id, db._db)
            result.append(worker._db_cache[db_id])
        else:
            result.append(pickle.loads(bytes.fromhex(arg)))
    return result


def create_executor(worker) -> callable:
    """Create an executor function for the given worker.

    The executor function has signature:
        (task_id, task_name, task_module, args) -> EXTaskExecResult

    It:
        1. Imports the user module
        2. Gets the function by task_name
        3. Retrieves the original function from _fly_original_func
        4. Deserializes arguments
        5. Calls the function

    Args:
        worker: Worker instance with get_database() method

    Returns:
        Executor function compatible with EXTaskExecutor
    """
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
            module = importlib.import_module(task_module)

            func = getattr(module, task_name)

            original_func = getattr(func, '_fly_original_func', func)

            deserialized_args = _deserialize_args(args, worker)

            for db_id, db_obj in worker._db_cache.items():
                if db_obj.is_frozen():
                    _frozen_before.add(db_id)

            output = original_func(*deserialized_args)

            frozen_dbs = []
            for db_id, db_obj in worker._db_cache.items():
                if db_obj.is_frozen() and db_id not in _frozen_before:
                    frozen_dbs.append(db_id)

            result['status'] = 0
            result['output'] = str(output) if output is not None else ""
            result['frozen_dbs'] = frozen_dbs

            logger.debug(
                f"Task executed successfully: id={task_id}, "
                f"name={task_name}, module={task_module}")

        except Exception as e:
            result['status'] = 1
            result['output'] = ""
            result['error'] = traceback.format_exc()

            logger.error(
                f"Task execution failed: id={task_id}, "
                f"name={task_name}, error={str(e)}\n{result.error}")

        return result

    return executor


__all__ = ['create_executor', '_deserialize_args']