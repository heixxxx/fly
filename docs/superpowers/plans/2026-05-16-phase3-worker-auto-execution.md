# Phase 3: Worker Auto-Execution Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Implement a single C++ binary (`fly`) that embeds Python, serves as both Master and Worker entry point, and supports script execution, interactive REPL, and automatic Worker subprocess spawning.

**Architecture:** C++ `main.cpp` parses CLI args, initializes Python interpreter via Python C API, sets `sys.path` to locate `.so` modules and Python package, executes `fly/main.py` for Python-side initialization, then runs user script or enters REPL. Workers are launched as subprocesses of the same binary with different CLI args. nanobind `.so` modules are loaded via normal Python import (not compiled into the binary).

**Tech Stack:** C++20 / Python 3.10 C API (`Python.h`) / nanobind (existing .so modules) / Bazel `cc_binary`

---

## File Structure

### New Files

| File | Responsibility |
|------|---------------|
| `src/main/cpp/main.cpp` | C++ entry: CLI parse, Py_Initialize, sys.path, run main.py, run script/REPL |
| `src/main/cpp/BUILD` | `cc_binary(name="fly")` target |
| `src/fly/main.py` | Python entry: log dir init, agent singleton creation, common API import |
| `src/fly/log_setup.py` | Log dir creation, rotation, symlink management |
| `src/fly/executor.py` | Worker real executor: import module, deserialize args, execute function |

### Modified Files

| File | Change |
|------|--------|
| `src/fly/runtime.py` | Remove env var detection; use module globals set by main.cpp |
| `src/fly/agent.py` | Master: add `_spawn_process_worker()`; Worker: real executor + submit |
| `src/fly/BUILD` | Add `main.py`, `log_setup.py`, `executor.py` to py_library |
| `src/fly/__init__.py` | Remove lazy agent import (main.py handles init) |
| `BUILD` (top-level) | Register `//src/main/cpp:fly` in compile_commands |
| `src/network/cpp/message_types.h` | Add `TaskSubmitMessage` struct (use reserved `TASK_SUBMIT=4`) |
| `src/agent/cpp/master_agent.h/cpp` | Handle `TaskSubmitMessage` + `DbPathRequestMessage` |
| `src/agent/cpp/worker_agent.h/cpp` | Handle `DbPathResponseMessage`, add `submit_task()` |
| `src/agent/export/agent_export.cpp` | Export `submit_task` on WorkerAgent, `register_database` on MasterAgent |

---

## Task 3.1: C++ Entry Point + BUILD + Python Embedding Verification

**Files:**
- Create: `src/main/cpp/main.cpp`
- Create: `src/main/cpp/BUILD`
- Modify: `BUILD` (top-level, add to compile_commands targets)

- [ ] **Step 1: Create BUILD target**

```python
# src/main/cpp/BUILD
package(default_visibility = ["//visibility:public"])

cc_binary(
    name = "fly",
    srcs = ["main.cpp"],
    copts = [
        "-std=c++20",
        "-I/usr/include/python3.10",
    ],
    linkopts = [
        "-lpython3.10",
        "-lpthread",
    ],
)
```

- [ ] **Step 2: Write minimal main.cpp to verify Python embedding**

```cpp
// src/main/cpp/main.cpp
#include <Python.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <filesystem>

static void setup_sys_path() {
    // Compute project root from binary location
    // Binary is at bazel-bin/src/main/cpp/fly
    // We need: bazel-bin/src/*/export/*.so and src/fly/
    std::filesystem::path exe_path = std::filesystem::canonical("/proc/self/exe");
    std::filesystem::path bazel_bin = exe_path.parent_path().parent_path().parent_path();
    std::filesystem::path project_root = bazel_bin.parent_path();

    std::string path_setup = R"(
import sys, os
)";
    path_setup += "sys.path.insert(0, '" + (bazel_bin / "src" / "core" / "export").string() + "')\n";
    path_setup += "sys.path.insert(0, '" + (bazel_bin / "src" / "storage" / "export").string() + "')\n";
    path_setup += "sys.path.insert(0, '" + (bazel_bin / "src" / "agent" / "export").string() + "')\n";
    path_setup += "sys.path.insert(0, '" + (bazel_bin / "src" / "log" / "export").string() + "')\n";
    path_setup += "sys.path.insert(0, '" + (bazel_bin / "src" / "network" / "export").string() + "')\n";
    path_setup += "sys.path.insert(0, '" + project_root.string() + "/src')\n";

    PyRun_SimpleString(path_setup.c_str());
}

int main(int argc, char* argv[]) {
    // Quick CLI parse for --worker mode check
    bool worker_mode = false;
    int worker_id = 0;
    std::string master_host = "127.0.0.1";
    int master_port = 0;
    std::string log_dir = "fly_log";
    bool interactive = false;
    std::string script_path;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--worker") == 0) {
            worker_mode = true;
        } else if (std::strcmp(argv[i], "--worker-id") == 0 && i + 1 < argc) {
            worker_id = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--master-host") == 0 && i + 1 < argc) {
            master_host = argv[++i];
        } else if (std::strcmp(argv[i], "--master-port") == 0 && i + 1 < argc) {
            master_port = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--log-dir") == 0 && i + 1 < argc) {
            log_dir = argv[++i];
        } else if (std::strcmp(argv[i], "-i") == 0) {
            interactive = true;
        } else if (argv[i][0] != '-') {
            script_path = argv[i];
        }
    }

    Py_Initialize();
    setup_sys_path();

    // Verify Python + .so modules work
    int rc = PyRun_SimpleString(
        "import _fly_core\n"
        "import _fly_storage\n"
        "import _fly_agent\n"
        "import _fly_log\n"
        "print('All Fly modules loaded successfully')\n"
    );

    if (rc != 0) {
        fprintf(stderr, "Failed to load Fly modules\n");
        Py_Finalize();
        return 1;
    }

    // Execute user script
    if (!script_path.empty()) {
        FILE* fp = std::fopen(script_path.c_str(), "r");
        if (!fp) {
            fprintf(stderr, "Cannot open script: %s\n", script_path.c_str());
            Py_Finalize();
            return 1;
        }
        PyRun_SimpleFile(fp, script_path.c_str());
        std::fclose(fp);
    }

    // Interactive REPL
    if (interactive || (script_path.empty() && !worker_mode)) {
        PyRun_InteractiveLoop(stdin, "<stdin>");
    }

    Py_Finalize();
    return 0;
}
```

- [ ] **Step 3: Build and verify**

Run: `./fly.sh buildonly //src/main/cpp:fly`
Expected: Build succeeds

- [ ] **Step 4: Run and verify Python embedding**

Run: `bazel-bin/src/main/cpp/fly -c "import _fly_core; print('OK')"`
Expected: Prints "All Fly modules loaded successfully" then enters REPL (since no script and no --worker)
Note: May need to adjust -c handling or just test with a script file

Run (alternate): `echo "import _fly_core; print('core OK')" > /tmp/test_embed.py && bazel-bin/src/main/cpp/fly /tmp/test_embed.py`
Expected: Prints "All Fly modules loaded successfully" then "core OK"

- [ ] **Step 5: Add to top-level BUILD compile_commands**

Add `"//src/main/cpp:fly"` to the `refresh_compile_commands` targets dict in `BUILD`.

- [ ] **Step 6: Build with fly.sh refresh**

Run: `./fly.sh build //src/main/cpp:fly`
Expected: Build succeeds, compile_commands.json updated

---

## Task 3.2: Log Directory Management

**Files:**
- Create: `src/fly/log_setup.py`

- [ ] **Step 1: Write log_setup.py**

```python
import os


def setup_log_dir(base_dir="fly_log"):
    if not os.path.exists(base_dir):
        os.makedirs(base_dir, exist_ok=True)
        _update_latest_symlink(base_dir)
        return base_dir

    # Existing dir → rotate: fly_log → fly_log.1, fly_log.1 → fly_log.2, ...
    num = 1
    while os.path.exists(f"{base_dir}.{num}"):
        num += 1
    os.rename(base_dir, f"{base_dir}.{num}")
    os.makedirs(base_dir, exist_ok=True)
    _update_latest_symlink(base_dir)
    return base_dir


def _update_latest_symlink(base_dir):
    latest = base_dir + ".latest"
    if os.path.islink(latest):
        os.unlink(latest)
    elif os.path.exists(latest):
        os.remove(latest)
    os.symlink(os.path.basename(base_dir), latest)
```

- [ ] **Step 2: Test log dir creation**

Run: `PYTHONPATH=src python3 -c "
import shutil, os
from fly.log_setup import setup_log_dir
shutil.rmtree('fly_log', ignore_errors=True)
shutil.rmtree('fly_log.1', ignore_errors=True)
d = setup_log_dir('fly_log')
assert os.path.isdir('fly_log')
assert os.path.islink('fly_log.latest')
print('PASS: basic create')
d = setup_log_dir('fly_log')
assert os.path.isdir('fly_log.1')
assert os.path.isdir('fly_log')
assert os.readlink('fly_log.latest') == 'fly_log'
print('PASS: rotation')
shutil.rmtree('fly_log', ignore_errors=True)
shutil.rmtree('fly_log.1', ignore_errors=True)
os.unlink('fly_log.latest')
print('All log_setup tests passed')
"`

- [ ] **Step 3: Add to BUILD**

Add `"log_setup.py"` to `srcs` list in `src/fly/BUILD`.

---

## Task 3.3: Python Entry Point (fly/main.py)

**Files:**
- Create: `src/fly/main.py`
- Modify: `src/fly/runtime.py` (remove env vars, add module globals)
- Modify: `src/fly/__init__.py` (simplify, remove lazy agent)
- Modify: `src/fly/BUILD` (add main.py)

- [ ] **Step 1: Rewrite runtime.py**

```python
import logging
from typing import Optional

from .agent import FlyAgent, Master, Worker

logger = logging.getLogger("fly")

_agent: Optional[FlyAgent] = None

_mode: str = "master"
_worker_id: int = 0
_master_host: str = "127.0.0.1"
_master_port: int = 0
_log_dir: str = "fly_log"


def configure_worker(worker_id: int, master_host: str, master_port: int,
                     log_dir: str = "fly_log"):
    global _mode, _worker_id, _master_host, _master_port, _log_dir
    _mode = "worker"
    _worker_id = worker_id
    _master_host = master_host
    _master_port = master_port
    _log_dir = log_dir


def configure_master(log_dir: str = "fly_log"):
    global _mode, _log_dir
    _mode = "master"
    _log_dir = log_dir


def get_agent() -> FlyAgent:
    global _agent
    if _agent is None:
        _agent = _create_agent()
    return _agent


def _create_agent() -> FlyAgent:
    if _mode == "worker":
        w = Worker(_worker_id, _master_host, _master_port)
        w.start()
        logger.debug(f"Worker mode: id={_worker_id}, master={_master_host}:{_master_port}")
        return w
    else:
        m = Master()
        logger.debug(f"Master mode: auto-initialized")
        return m


def reset():
    global _agent
    if _agent is not None:
        _agent.stop()
        _agent = None


__all__ = ['get_agent', 'reset', 'configure_worker', 'configure_master']
```

- [ ] **Step 2: Write main.py**

```python
import os
import sys
import logging

from .log_setup import setup_log_dir


def init(log_dir="fly_log", worker_mode=False, worker_id=0,
         master_host="127.0.0.1", master_port=0):
    logging.basicConfig(level=logging.DEBUG)

    log_dir = setup_log_dir(log_dir)

    if worker_mode:
        from _fly_log import init_worker
        init_worker(worker_id, log_dir + "/")
        from .runtime import configure_worker
        configure_worker(worker_id, master_host, master_port, log_dir)
    else:
        from _fly_log import init_master
        init_master(log_dir + "/")
        from .runtime import configure_master
        configure_master(log_dir)

    from .runtime import get_agent
    agent = get_agent()
    print(f"Fly initialized: mode={agent.mode}", file=sys.stderr)


__all__ = ['init']
```

- [ ] **Step 3: Simplify __init__.py**

```python
from .database import Database
from .config import get_config
from .task import as_task, task_name
from .runtime import get_agent
from .agent import Master, Worker, FlyAgent


def __getattr__(name):
    if name == "agent":
        return get_agent()
    raise AttributeError(f"module 'fly' has no attribute {name}")


__all__ = [
    'Database', 'agent', 'get_agent', 'get_config',
    'as_task', 'task_name',
    'Master', 'Worker', 'FlyAgent',
]
```

- [ ] **Step 4: Add files to BUILD**

Add `"main.py"` and `"log_setup.py"` to `srcs` in `src/fly/BUILD`.

- [ ] **Step 5: Update main.cpp to call fly.main.init()**

Add after `setup_sys_path()` in main.cpp, before script execution:

```cpp
// Set fly mode via module globals, then call fly.main.init()
if (worker_mode) {
    PyRun_SimpleString(
        "import fly.main\n"
        "fly.main.init(worker_mode=True, worker_id=WORKER_ID, "
        "master_host='HOST', master_port=PORT, log_dir='LOGDIR')"
    );
    // Replace WORKER_ID, HOST, PORT, LOGDIR with actual values
} else {
    PyRun_SimpleString(
        "import fly.main\n"
        "fly.main.init(log_dir='LOGDIR')"
    );
}
```

(Actual implementation uses string formatting with the parsed values.)

- [ ] **Step 6: Build and verify**

Run: `./fly.sh build //src/main/cpp:fly`
Run: `echo "from fly import Database; print('OK')" > /tmp/test_init.py && bazel-bin/src/main/cpp/fly /tmp/test_init.py`
Expected: Prints "Fly initialized: mode=master" then "OK"

---

## Task 3.4: Worker Real Executor

**Files:**
- Create: `src/fly/executor.py`

- [ ] **Step 1: Write executor.py**

```python
import pickle
import importlib
import traceback
import logging

logger = logging.getLogger("fly")


def create_executor(worker):
    """Create a real executor function for a Worker process."""
    from _fly_agent import EXTaskExecResult, EXTaskExecStatus as Status

    def executor(task_id, task_name, task_module, args):
        ret = EXTaskExecResult()
        ret.task_id = task_id
        try:
            module = importlib.import_module(task_module)
            func_wrapper = getattr(module, task_name, None)
            if func_wrapper is None:
                raise RuntimeError(f"Function '{task_name}' not found in module '{task_module}'")

            original_func = getattr(func_wrapper, '_fly_original_func', func_wrapper)
            deserialized = _deserialize_args(args, worker)
            result = original_func(*deserialized)

            ret.status = Status.SUCCESS
            ret.output = str(result) if result is not None else ""
            ret.error = ""
            ret.outputs = []
        except Exception as e:
            ret.status = Status.FAILED
            ret.output = ""
            ret.error = traceback.format_exc()
            ret.outputs = []
            logger.error(f"Task {task_id} failed: {e}")
        return ret

    return executor


def _deserialize_args(args, worker):
    result = []
    for arg in args:
        if arg.startswith("__fly_db__:"):
            db_id = arg[len("__fly_db__:"):]
            db = worker.get_database(db_id)
            result.append(db)
        else:
            obj = pickle.loads(bytes.fromhex(arg))
            result.append(obj)
    return result


__all__ = ['create_executor']
```

- [ ] **Step 2: Verify deserialize logic standalone**

Run: `PYTHONPATH=src python3 -c "
import sys
sys.path.insert(0, 'bazel-bin/src/storage/export')
from fly.task import _serialize_args
from fly.executor import _deserialize_args

# Mock worker
class MockWorker:
    def get_database(self, db_id):
        return f'Database({db_id})'

worker = MockWorker()
original = ['hello', 42, 3.14]
serialized = [pickle.dumps(a).hex() for a in original]
deserialized = _deserialize_args(serialized, worker)
assert deserialized == original, f'{deserialized} != {original}'
print('PASS: basic round-trip')

db_marker = '__fly_db__:abc123'
serialized2 = _serialize_args(['dummy_db', 'hello'])
# First arg would be a db marker if we had a real Database
print('PASS: executor module imports')
"`

---

## Task 3.5: Master Spawn Process Worker + Worker Real Start

**Files:**
- Modify: `src/fly/agent.py` (Master: `_spawn_process_worker`; Worker: real executor + `start()`)

- [ ] **Step 1: Update Worker class — real executor + submit**

Replace `Worker` class in `src/fly/agent.py`:

```python
class Worker(FlyAgent):

    @property
    def mode(self) -> str:
        return "worker"

    def __init__(self, worker_id: int, master_host: str, master_port: int):
        self._agent = EXAgentWorker(worker_id, master_host, master_port)
        self._db_cache = {}
        self._master_host = master_host
        self._master_port = master_port
        self._worker_id = worker_id

    def start(self):
        from .executor import create_executor
        from _fly_agent import EXTaskExecutor

        executor = EXTaskExecutor()
        executor.set_exec_func(create_executor(self))
        self._agent.set_executor(executor)
        self._agent.start()
        logger.debug(
            f"Worker {self._worker_id} started, "
            f"connected to {self._master_host}:{self._master_port}")

    def submit(self, name: str, module: str, args: list,
               inputs: list = None):
        self._agent.submit_task(name, module, args, inputs or [])
        logger.debug(f"Worker submitted task: name={name}")

    def get_database(self, db_id: str):
        if db_id not in self._db_cache:
            raise RuntimeError(
                f"Unknown db_id: {db_id}, need master info (Phase 3)")
        return self._db_cache[db_id]

    def stop(self):
        self._agent.stop()
```

- [ ] **Step 2: Add `_spawn_process_worker` to Master**

Add to `Master` class:

```python
    def _spawn_process_worker(self, worker_id: int):
        import subprocess
        import sys

        binary_path = self._find_binary()

        cmd = [
            binary_path,
            "--worker",
            "--worker-id", str(worker_id),
            "--master-host", self._host,
            "--master-port", str(self._port),
            "--log-dir", self._log_dir if hasattr(self, '_log_dir') else "fly_log",
        ]
        proc = subprocess.Popen(cmd)
        self._worker_procs.append(proc)
        logger.debug(f"Spawned worker {worker_id}: pid={proc.pid}")

    @staticmethod
    def _find_binary():
        import os
        override = os.environ.get("FLY_BINARY_PATH")
        if override:
            return override
        path = os.path.abspath(sys.argv[0])
        if os.path.isfile(path):
            return path
        raise RuntimeError("Cannot locate fly binary. Set FLY_BINARY_PATH.")
```

- [ ] **Step 3: Update `launch_local_workers` to use process workers**

Replace method:

```python
    def launch_local_workers(self, worker_configs: list, mode: str = "process"):
        self.start()
        self._port = self._agent.get_port()

        num_workers = len(worker_configs)
        if mode == "thread":
            for i in range(num_workers):
                self._start_thread_worker(i + 1)
        else:
            for i in range(num_workers):
                self._spawn_process_worker(i + 1)

        logger.debug(
            f"Master running on {self._host}:{self._port}, "
            f"{num_workers} workers launched ({mode})")
```

Also add `_worker_procs` to `__init__`:

```python
    def __init__(self, host: str = "127.0.0.1", port: int = 0):
        self._agent = EXAgentMaster(host, port)
        self._task_counter = 0
        self._lock = threading.Lock()
        self._workers = []
        self._worker_procs = []
        self._host = host
        self._port = port
        self._running = False
```

- [ ] **Step 4: Build and run existing tests**

Run: `PYTHONPATH=src python3 src/fly/tests/test_fly_api.py`
Expected: All 3 tests pass (thread mode still works)

---

## Task 3.6: DbPathRequest/Response Messages

**Files:**
- Modify: `src/network/cpp/message_types.h` (add message structs)
- Modify: `src/agent/cpp/master_agent.h/cpp` (handle DbPathRequest, add db registry)
- Modify: `src/agent/cpp/worker_agent.h/cpp` (handle DbPathResponse)
- Modify: `src/agent/export/agent_export.cpp` (export register_database)

- [ ] **Step 1: Add message structs to message_types.h**

Reuse `DATA_QUERY` (9) for DbPathRequest and `DATA_LOCATION` (10) for DbPathResponse:

```cpp
// Worker → Master: query database path by db_id
struct DbPathRequestMessage {
    MessageHeader header;
    CMString db_id;

    static constexpr MessageType msg_type = MessageType::DATA_QUERY;

    FLY_SERIALIZE(header, db_id);
};

// Master → Worker: return database paths
struct DbPathResponseMessage {
    MessageHeader header;
    CMString db_id;
    CMString base_path;
    CMString data_path;

    static constexpr MessageType msg_type = MessageType::DATA_LOCATION;

    FLY_SERIALIZE(header, db_id, base_path, data_path);
};
```

- [ ] **Step 2: Add db registry to MasterAgent**

In `master_agent.h`:
```cpp
    void register_database(const CMString& db_id, const CMString& base_path,
                           const CMString& data_path = "");

private:
    CMMap<CMString, CMMap<CMString, CMString>> db_registry_;
```

In `master_agent.cpp`, add handler for DATA_QUERY and implement `register_database`:

```cpp
void MasterAgent::register_database(const CMString& db_id,
                                     const CMString& base_path,
                                     const CMString& data_path) {
    db_registry_[db_id] = {{"base_path", base_path}, {"data_path", data_path}};
}
```

Register handler in `start()`:
```cpp
reactor_->register_handler<DbPathRequestMessage>(
    [this](uint64_t conn_id, const DbPathRequestMessage& msg) {
        DbPathResponseMessage resp;
        resp.db_id = msg.db_id;
        auto it = db_registry_.find(msg.db_id);
        if (it != db_registry_.end()) {
            resp.base_path = it->second.at("base_path");
            resp.data_path = it->second.count("data_path") ? it->second.at("data_path") : "";
        }
        reactor_->send(conn_id, resp);
    });
```

- [ ] **Step 3: Handle DbPathResponse in WorkerAgent**

In `worker_agent.h`, add public method:
```cpp
    CMMap<CMString, CMString> db_path_cache_;
```

Register handler in `start()`:
```cpp
reactor_->register_handler<DbPathResponseMessage>(
    [this](uint64_t conn_id, const DbPathResponseMessage& msg) {
        db_path_cache_[msg.db_id] = msg.base_path;
    });
```

- [ ] **Step 4: Export `register_database` to Python**

Add to agent_export.cpp in MasterAgent section:
```cpp
FLY_EXPORT_METHOD("register_database", [](fly::MasterAgent& self,
                                           const fly::CMString& db_id,
                                           const fly::CMString& base_path,
                                           const fly::CMString& data_path) {
    self.register_database(db_id, base_path, data_path);
})
```

- [ ] **Step 5: Build and test**

Run: `./fly.sh test //src/...`
Expected: All existing tests pass (no regressions)

---

## Task 3.7: TaskSubmitMessage (Worker→Master Recursive Submit)

**Files:**
- Modify: `src/network/cpp/message_types.h` (add TaskSubmitMessage using TASK_SUBMIT=4)
- Modify: `src/agent/cpp/worker_agent.h/cpp` (add submit_task method)
- Modify: `src/agent/cpp/master_agent.h/cpp` (handle TaskSubmitMessage)
- Modify: `src/agent/export/agent_export.cpp` (export submit_task on WorkerAgent)

- [ ] **Step 1: Add TaskSubmitMessage struct**

```cpp
// Worker → Master: submit task from within worker
struct TaskSubmitMessage {
    MessageHeader header;
    CMString task_name;
    CMString task_module;
    CMVector<CMString> args;
    CMVector<CMString> inputs;

    static constexpr MessageType msg_type = MessageType::TASK_SUBMIT;

    FLY_SERIALIZE(header, task_name, task_module, args, inputs);
};
```

- [ ] **Step 2: WorkerAgent.submit_task()**

```cpp
void WorkerAgent::submit_task(const CMString& name, const CMString& module,
                               const CMVector<CMString>& args,
                               const CMVector<CMString>& inputs) {
    TaskSubmitMessage msg;
    msg.task_name = name;
    msg.task_module = module;
    msg.args = args;
    msg.inputs = inputs;
    reactor_->send(master_conn_, msg);
}
```

- [ ] **Step 3: MasterAgent handle TaskSubmitMessage**

Register handler in `start()`:
```cpp
reactor_->register_handler<TaskSubmitMessage>(
    [this](uint64_t conn_id, const TaskSubmitMessage& msg) {
        static uint64_t remote_task_counter = 100000;
        uint64_t task_id = ++remote_task_counter;
        submit_task(task_id, msg.task_name, msg.task_module,
                    msg.args, msg.inputs, {});
    });
```

- [ ] **Step 4: Export submit_task on WorkerAgent**

```cpp
FLY_EXPORT_METHOD("submit_task", [](fly::WorkerAgent& self,
                                     const fly::CMString& name,
                                     const fly::CMString& module,
                                     const fly::CMVector<fly::CMString>& args,
                                     const fly::CMVector<fly::CMString>& inputs) {
    self.submit_task(name, module, args, inputs);
})
```

- [ ] **Step 5: Build and test**

Run: `./fly.sh test //src/...`

---

## Task 3.8: Master Shutdown Broadcast + Worker Heartbeat Timeout

**Files:**
- Modify: `src/agent/cpp/master_agent.cpp` (broadcast ShutdownMessage in stop())

- [ ] **Step 1: Broadcast shutdown to all connected workers**

In `MasterAgent::stop()`, before stopping reactor:
```cpp
ShutdownMessage msg;
for (auto& [wid, conn_id] : worker_to_conn_) {
    reactor_->send(conn_id, msg);
}
```

- [ ] **Step 2: Build and test**

Run: `./fly.sh test //src/...`

---

## Task 3.9: End-to-End Integration Test

**Files:**
- Create: `src/fly/tests/test_e2e.py`

- [ ] **Step 1: Write end-to-end test**

```python
import sys
import os
import time
import shutil
import subprocess

FLY_BIN = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                       '../../../bazel-bin/src/main/cpp/fly')

def test_master_worker_process():
    script = """
import time
from fly import Database, as_task, task_name, get_agent

master = get_agent()
master.launch_local_workers([{"role": "hybrid"}])
time.sleep(1)

@as_task()
@task_name("hello_task")
def hello():
    return "world"

hello()
time.sleep(2)
print(f"completed={master.completed_tasks}")
assert len(master.completed_tasks) >= 1
master.stop()
"""
    script_path = "/tmp/fly_e2e_test.py"
    with open(script_path, "w") as f:
        f.write(script)

    try:
        result = subprocess.run(
            [FLY_BIN, script_path],
            capture_output=True, text=True, timeout=30
        )
        print(f"stdout: {result.stdout}")
        print(f"stderr: {result.stderr}")
        assert result.returncode == 0, f"Exit code: {result.returncode}"
        assert "completed=" in result.stdout or "completed=" in result.stderr
        print("PASS: test_master_worker_process")
    finally:
        os.unlink(script_path)
        shutil.rmtree("fly_log", ignore_errors=True)
        if os.path.islink("fly_log.latest"):
            os.unlink("fly_log.latest")

if __name__ == "__main__":
    test_master_worker_process()
    print("E2E test passed!")
```

- [ ] **Step 2: Build fly binary**

Run: `./fly.sh build //src/main/cpp:fly`

- [ ] **Step 3: Run E2E test**

Run: `python3 src/fly/tests/test_e2e.py`
Expected: PASS

- [ ] **Step 4: Run full test suite**

Run: `./fly.sh test //src/...`
Expected: All existing tests pass + new E2E test passes

---

## Execution Dependencies

```
3.1 (main.cpp + BUILD) ──┬── 3.3 (main.py + runtime.py rewrite) ── 3.4 (executor.py)
                         │                                            │
3.2 (log_setup.py) ─────┘                                            │
                                                                      ├── 3.5 (spawn worker)
3.6 (DbPath messages) ───────────────────────────────────────────────┤
3.7 (TaskSubmitMessage) ─────────────────────────────────────────────┤
3.8 (Shutdown broadcast) ────────────────────────────────────────────┤
                                                                      │
                                                                      └── 3.9 (E2E test)
```

Tasks 3.2, 3.6, 3.7, 3.8 can be parallelized after 3.1 completes.
