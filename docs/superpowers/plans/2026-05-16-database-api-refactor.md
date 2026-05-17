# Database Python API Refactoring Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Unify Database creation via `fly.open_db()`, absorb FlyDatabase's C++ type-aware serialization into Database, and clean up legacy code.

**Architecture:** `fly.open_db(path)` is the sole public factory. `Database.__init__` becomes internal (prefixed `_`). `write_object`/`read_object` gain dual-path serialization for C++ exported types. Legacy `FlyDatabase` is deleted. All call sites updated.

**Tech Stack:** Python 3, nanobind C++ bindings, pickle, bitsery serialization

---

## File Structure

| Action | File | Responsibility |
|--------|------|----------------|
| Modify | `src/fly/database.py` | Absorb C++ type awareness + add missing proxy methods |
| Modify | `src/fly/__init__.py` | Add `open_db()`, remove `Database` from public exports |
| Modify | `src/fly/executor.py` | Update internal Database construction (use `_Database`) |
| Modify | `src/e2e_user_script.py` | Replace `Database(path)` with `fly.open_db(path)` |
| Modify | `src/fly/tests/test_fly_api.py` | Replace `Database(path)` with `fly.open_db(path)` |
| Modify | `qa/storage_test.py` | Replace `FlyDatabase` with `fly.open_db()` via `Database` |
| Delete | `src/storage/py/database.py` | Remove legacy FlyDatabase |
| Modify | `src/storage/py/__init__.py` | Remove FlyDatabase import/export |

---

### Task 1: Enhance Database with C++ type awareness + missing proxy methods

**Files:**
- Modify: `src/fly/database.py`

This task absorbs FlyDatabase's dual-path serialization and adds the 6 missing proxy methods.

- [ ] **Step 1: Update `src/fly/database.py`**

Replace the entire file with:

```python
import pickle
from _fly_storage import ex_stg_create_database


class _Database:

    def __init__(self, base_path: str, data_path: str = "", writer_id: int = 0):
        from .runtime import _mode
        if _mode == "master":
            from .runtime import get_agent
            agent = get_agent()
            self._db = agent._agent.get_or_create_database(base_path, data_path, writer_id)
        else:
            self._db = ex_stg_create_database(base_path, data_path, writer_id)

    def write_object(self, name: str, obj) -> str:
        if hasattr(obj, "is_cpp"):
            data = obj.__getstate__()
        else:
            data = pickle.dumps(obj, -1)
        return self._db._write_typed(name, data, type(obj).__name__)

    def read_object(self, name: str):
        try:
            data, py_name = self._db._read_typed(name)
        except Exception:
            return self._read_remote(name)
        import _fly_storage
        cls = getattr(_fly_storage, py_name, None)
        if cls is not None and hasattr(cls, "is_cpp"):
            obj = cls.__new__(cls)
            obj.__setstate__(data)
            return obj
        return pickle.loads(data)

    def _read_remote(self, name: str):
        from .runtime import get_agent
        agent = get_agent()
        data, py_name = agent._agent.request_remote_data(name)
        import _fly_storage
        cls = getattr(_fly_storage, py_name, None)
        if cls is not None and hasattr(cls, "is_cpp"):
            obj = cls.__new__(cls)
            obj.__setstate__(data)
            return obj
        return pickle.loads(data)

    def write_object_raw(self, name: str, data: str) -> str:
        return self._db.write_object_raw(name, data)

    def read_object_raw(self, name: str) -> str:
        return self._db.read_object_raw(name)

    def get_obj_name(self, name: str) -> str:
        return self._db.get_obj_name(name)

    def get_db_id(self) -> str:
        return self._db.get_db_id()

    def get_base_path(self) -> str:
        return self._db.get_base_path()

    def get_data_path(self) -> str:
        return self._db.get_data_path()

    def freeze(self):
        self._db.freeze()

    def is_frozen(self) -> bool:
        return self._db.is_frozen()

    def load_meta(self):
        return self._db.load_meta()

    def reset(self):
        self._db.reset()

    def __repr__(self):
        return f"Database(db_id={self.get_db_id()})"
```

Key changes:
- Class renamed `Database` → `_Database` (internal)
- `write_object`: added `is_cpp` check → `obj.__getstate__()` path
- `read_object`: added C++ type reconstruction via `getattr(_fly_storage, py_name)` + `__new__`/`__setstate__`
- `_read_remote`: same C++ type reconstruction logic
- Added proxy methods: `write_object_raw`, `read_object_raw`, `get_base_path`, `get_data_path`, `load_meta`, `reset`

---

### Task 2: Add `open_db()` factory to fly package

**Files:**
- Modify: `src/fly/__init__.py`

- [ ] **Step 1: Update `src/fly/__init__.py`**

```python
from .database import _Database
from .config import get_config
from .task import as_task, task_name
from .runtime import get_agent
from .agent import Master, Worker, FlyAgent


def open_db(path: str, data_path: str = "") -> _Database:
    return _Database(path, data_path)


def __getattr__(name):
    if name == "agent":
        return get_agent()
    raise AttributeError(f"module 'fly' has no attribute {name}")


__all__ = [
    'open_db', 'agent', 'get_agent', 'get_config',
    'as_task', 'task_name',
    'Master', 'Worker', 'FlyAgent',
]
```

Key changes:
- Import `_Database` instead of `Database`
- Add `open_db(path, data_path="")` factory function
- Remove `Database` from `__all__`

---

### Task 3: Update all call sites to use `open_db()`

**Files:**
- Modify: `src/e2e_user_script.py`
- Modify: `src/fly/tests/test_fly_api.py`
- Modify: `src/fly/executor.py` (internal — keep `_Database` direct construction)

- [ ] **Step 1: Update `src/e2e_user_script.py`**

Replace:
```python
from fly import Database
```
With:
```python
from fly import open_db
```

Replace all `Database(DB_PATH)` calls with `open_db(DB_PATH)`:
- Line 46: `db = open_db(DB_PATH)`
- Line 87: `db2 = open_db(DB_PATH + "_frozen")`
- Line 107: `db3 = open_db(DB_PATH + "_blocked")`
- Line 126: `db4 = open_db(DB_PATH + "_fanout")`

- [ ] **Step 2: Update `src/fly/tests/test_fly_api.py`**

Replace:
```python
from fly import Database, as_task, task_name
```
With:
```python
from fly import open_db, as_task, task_name
```

Replace all `Database(...)` calls with `open_db(...)`:
- Line 25: `db = open_db("/tmp/fly_api_test_db1")`
- Line 34: `db = open_db("/tmp/fly_api_test_db2")`

- [ ] **Step 3: Update `src/fly/executor.py`**

This is internal code — keep direct `_Database` construction.

Replace:
```python
from fly.database import Database
```
With:
```python
from fly.database import _Database
```

Replace line 26:
```python
db = Database(base_path, data_path, worker._worker_id)
```
With:
```python
db = _Database(base_path, data_path, worker._worker_id)
```

- [ ] **Step 4: Build and verify compilation**

Run: `./fly.sh buildonly //src/...`
Expected: Build succeeds

- [ ] **Step 5: Run C++ unit tests**

Run: `./fly.sh test //src/...`
Expected: All 31 tests pass

- [ ] **Step 6: Run E2E tests**

Run: `rm -rf /tmp/fly_e2e_db* fly_log && timeout 120 ./bazel-bin/src/main/cpp/fly src/e2e_user_script.py`
Expected: 5/5 E2E tests pass, clean exit

- [ ] **Step 7: Run fly API tests**

Run: `cd src && python -m fly.tests.test_fly_api`
Expected: All 3 tests pass

---

### Task 4: Update qa/storage_test.py to use open_db()

**Files:**
- Modify: `qa/storage_test.py`

- [ ] **Step 1: Update imports**

Remove the `_storage_py` path hack (lines 18-20):
```python
_storage_py = os.path.join(os.path.dirname(__file__), '..', 'src', 'storage', 'py')
if os.path.exists(_storage_py):
    sys.path.insert(0, _storage_py)
```

Add `fly` package path and `open_db` import. Add after the `_bazel_bin` path setup:
```python
_fly_src = os.path.join(os.path.dirname(__file__), '..', 'src')
if os.path.exists(_fly_src):
    sys.path.insert(0, _fly_src)
```

Replace all `from database import FlyDatabase` with `from fly import open_db`.

Replace all `db = FlyDatabase(temp_dir)` with `db = open_db(temp_dir)`.

The following test functions need updating (10 occurrences of `FlyDatabase`):
- `test_fly_database_cpp_class_write_read` (lines 131, 134)
- `test_fly_database_cpp_dbmeta_write_read` (lines 160, 163)
- `test_fly_database_python_class_write_read` (lines 195, 197)
- `test_fly_database_mixed_cpp_python` (lines 213, 216)
- `test_fly_database_multiple_cpp_types` (lines 277, 280)
- `test_fly_database_pickle_roundtrip` (lines 307, 309)
- `test_cpp_writes_python_reads_via_flydatabase` (lines 358, 361)

Note: `test_cpp_writes_python_reads_via_flydatabase` passes `db` (a `_Database`) to `ex_stg_cpp_write_index_entry()`. Since `_Database` is NOT a subclass of `EXStgDatabase`, this test needs special handling — it must pass `db._db` (the underlying C++ object) instead. Update that test:

```python
def test_cpp_writes_python_reads_via_flydatabase(temp_dir):
    from fly import open_db
    from _fly_storage import ex_stg_cpp_write_index_entry, EXStgIndexEntry

    db = open_db(temp_dir)

    ex_stg_cpp_write_index_entry(db._db, "cross/fly_entry")

    result = db.read_object("cross/fly_entry")
    assert isinstance(result, EXStgIndexEntry)
    assert result.object_name == "cross/fly_entry"
    assert result.file_name == "cpp_generated.dat"
    assert result.offset == 12345
    assert result.size == 67890
```

- [ ] **Step 2: Run qa/storage_test.py**

Run: `cd /root/fly && python -m pytest qa/storage_test.py -v`
Expected: All tests pass (may need to check path setup)

---

### Task 5: Delete legacy FlyDatabase

**Files:**
- Delete: `src/storage/py/database.py`
- Modify: `src/storage/py/__init__.py`

- [ ] **Step 1: Delete `src/storage/py/database.py`**

- [ ] **Step 2: Update `src/storage/py/__init__.py`**

Remove FlyDatabase import and export:
```python
from _fly_storage import (
    EXStgStorageManager,
    EXStgIndexEntry, EXStgDbMeta, EXStgWorkerInfo,
    EXStgCompressionType,
    ex_stg_get_storage_manager, ex_stg_create_database,
)

__all__ = [
    'EXStgStorageManager',
    'EXStgIndexEntry', 'EXStgDbMeta', 'EXStgWorkerInfo',
    'EXStgCompressionType',
    'ex_stg_get_storage_manager', 'ex_stg_create_database',
]
```

- [ ] **Step 3: Build and verify**

Run: `./fly.sh buildonly //src/...`
Expected: Build succeeds

- [ ] **Step 4: Full test suite**

Run: `./fly.sh test //src/...`
Expected: All 31 tests pass

Run: `rm -rf /tmp/fly_e2e_db* fly_log && timeout 120 ./bazel-bin/src/main/cpp/fly src/e2e_user_script.py`
Expected: 5/5 E2E tests pass, clean exit

---

### Task 6: Update test_fly_api to not import Database

**Files:**
- Modify: `src/fly/tests/test_fly_api.py`

- [ ] **Step 1: Verify test_fly_api works with open_db**

Run: `cd /root/fly/src && python -m fly.tests.test_fly_api`
Expected: All 3 tests pass

If `test_serialize_args` fails because `_serialize_args` checks for `Database` type, check `src/fly/task.py` for type checks and update to accept `_Database`.

---

## Self-Review Checklist

- [x] Spec coverage: Each requirement (open_db, C++ type awareness, missing methods, legacy cleanup, call site updates) has a corresponding task
- [x] Placeholder scan: No TBD/TODO/placeholders — all code shown inline
- [x] Type consistency: `_Database` used consistently in executor, `open_db()` used in all public call sites, `db._db` for C++ cross-language test
