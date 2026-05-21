# Python API 模块

## 模块概述

**位置**: `src/fly/`

Python API 层将 C++ 底层 API 包装为用户友好的高层接口，提供任务定义、Database 操作、Agent 管理和运行时配置。

---

## 核心文件

| 文件 | 说明 |
|------|------|
| `__init__.py` | 顶层包，导出 open_db, as_task, get_config 等 |
| `agent.py` | Master/Worker Python 封装 |
| `database.py` | _Database 类（三层读取） |
| `task.py` | @as_task 和 @task_name 装饰器 |
| `executor.py` | Worker 任务执行器 |
| `runtime.py` | 运行时配置（master/worker mode） |
| `config.py` | Config Python 封装 |
| `main.py` | 初始化入口 |

---

## 类/函数详细说明

### open_db(path, data_path) — Database 工厂

```python
def open_db(path: str, data_path: str = "") -> _Database:
    return _Database(path, data_path)
```

唯一公开的 Database 创建接口。

### _Database — Database 内部类

```python
class _Database:
    def __init__(self, base_path, data_path="", writer_id=0):
        # Master 模式: agent._agent.get_or_create_database(...)
        # Worker 模式: ex_stg_create_database(...)

    def write_object(self, name: str, obj) -> str:
        # 自动检测 is_cpp → __getstate__() 或 pickle.dumps

    def read_object(self, name: str):
        # 三层降级读取
        # Layer 1: DataService.try_read_local → 本地
        # Layer 2: lookup_remote_idx → DataClient 直连
        # Layer 3: request_remote_data → 全程远程 (最多 3 次重试)

    def get_obj_name(self, name: str) -> str:
        # 返回 "{db_id}:{name}" 唯一标识符

    def get_db_id(self) -> str
    def freeze(self)
    def is_frozen(self) -> bool
    def write_object_raw(self, name, data) -> str
    def read_object_raw(self, name) -> str
    def load_meta(self)
    def get_base_path(self) -> str
    def get_data_path(self) -> str
    def reset(self)
```

**C++ 类型感知序列化**:

```python
def write_object(self, name, obj):
    if hasattr(obj, "is_cpp"):
        data = obj.__getstate__()     # C++ 导出类型 → bitsery 序列化
    else:
        data = pickle.dumps(obj, -1)  # 纯 Python 对象 → pickle
    return self._db._write_typed(name, data, type(obj).__name__)

def _reconstruct(self, data, py_name):
    cls = getattr(_fly_storage, py_name, None)
    if cls and hasattr(cls, "is_cpp"):
        obj = cls.__new__(cls)
        obj.__setstate__(data)        # C++ 类型 → bitsery 反序列化
        return obj
    return pickle.loads(data)          # Python 对象 → pickle 反序列化
```

---

### as_task(inputs) — 任务装饰器

```python
def as_task(inputs=None):
    def decorator(func):
        name = getattr(func, '_fly_task_name', None) or func.__name__
        module = func.__module__ or "__main__"

        def wrapper(*args, **kwargs):
            agent = get_agent()
            task_inputs = inputs(*args, **kwargs) if inputs else []
            serialized = _serialize_args(args)
            agent.submit(name, module, serialized, task_inputs)

        wrapper._fly_original_func = func
        wrapper._fly_task_name = name
        return wrapper
    return decorator
```

**参数序列化**:

```python
def _serialize_args(args):
    result = []
    for arg in args:
        if hasattr(arg, 'get_db_id'):   # Database 对象
            result.append(f"__fly_db__:{db_id}:{base_path}:{data_path}")
        else:
            result.append(pickle.dumps(arg).hex())
    return result
```

> **注意**: inputs 使用 `db.get_obj_name()` 生成 full name（`db_id:object_name`），确保与 DataService / DependencyGraph 命名空间一致。

```python
# 正确
@as_task(inputs=lambda db, key: [db.get_obj_name(f"input/{key}")])

# 错误 — 短名无法匹配 DataService 索引
@as_task(inputs=lambda db, key: [f"input/{key}"])
```

---

### task_name(name) — 任务命名装饰器

```python
def task_name(name: str):
    def decorator(func):
        func._fly_task_name = name
        return func
    return decorator
```

---

### Master — Master Agent 封装

```python
class Master(FlyAgent):
    def __init__(self, host="127.0.0.1", port=0):
        self._agent = EXAgentMaster(host, port)

    def start(self):
        self._agent.start()
        self._port = self._agent.get_port()

    def submit(self, name, module, args, inputs=None) -> int:
        # 线程安全 task_counter
        # agent.submit_task_with_deps(task_id, ...)

    def launch_local_workers(self, worker_configs, port=None, mode="thread"):
        # mode="thread": 线程内 Worker (_start_thread_worker)
        # mode="process": 子进程 Worker (_spawn_process_worker)

    def stop(self):
        # 停止所有 Worker → 停止 Agent

    def restart_failed_tasks(self, file_path: str):
        # 读取失败任务记录，检查数据可用性，重新提交

    @property
    def pending_tasks / running_tasks / completed_tasks
```

### Worker — Worker Agent 封装

```python
class Worker(FlyAgent):
    def __init__(self, worker_id, master_host, master_port):
        self._agent = EXAgentWorker(worker_id, master_host, master_port)
        self._db_cache = {}            # db_id → _Database

    def start(self):
        self._executor = EXTaskExecutor()
        self._executor.set_exec_func(create_executor(self))
        self._agent.set_executor(self._executor)
        self._agent.start()

    def submit(self, name, module, args, inputs=None) -> int:
        # 递归任务提交: agent.submit_task(...)

    def get_database(self, db_id):
        return self._db_cache[db_id]

    def set_worker_property(self, prop):
        # 设置 Worker 属性（GPU/CPU等）

    def remove_worker_property(self, prop):
        # 移除 Worker 属性

    def get_worker_properties(self) -> list:
        # 获取所有 Worker 属性列表
```

---

### create_executor(worker) — Worker 执行器

```python
def create_executor(worker) -> callable:
    def executor(task_id, task_name, task_module, args):
        # 1. importlib.import_module(task_module)
        # 2. func = getattr(module, task_name)
        # 3. original_func = func._fly_original_func
        # 4. deserialized_args = _deserialize_args(args, worker)
        #    - "__fly_db__:" → 创建/获取 _Database
        #    - hex string → pickle.loads
        # 5. output = original_func(*deserialized_args)
        # 6. 检测 frozen_dbs (执行前后对比)
        # 7. 返回 {task_id, status, output, error, frozen_dbs}
    return executor
```

---

### runtime — 运行时配置

```python
_mode: str = "master"     # "master" or "worker"
_agent: FlyAgent = None   # 全局 Agent 实例

def get_agent() -> FlyAgent:       # 懒初始化 Agent
def configure_worker(...)          # 设置 Worker 模式
def configure_master(...)          # 设置 Master 模式
def reset()                        # 重置 Agent
```

**FlyAgent 抽象基类**:
```python
from fly.agent import FlyAgent

class FlyAgent(ABC):
    @abstractmethod
    def start(self): pass

    @abstractmethod
    def stop(self): pass

    @abstractmethod
    def submit_task_with_deps(self, task_id, name, module, args, inputs,
                             outputs, required_capabilities, config):
        pass

    @abstractmethod
    def get_or_create_database(self, base_path, data_path, writer_id):
        pass

    @abstractmethod
    def get_pending_tasks(self): pass

    @abstractmethod
    def get_running_tasks(self): pass

    @abstractmethod
    def get_completed_tasks(self): pass

    @abstractmethod
    def get_failed_tasks(self): pass

    @abstractmethod
    def get_task_error(self, task_id): pass

    # Worker-only
    @abstractmethod
    def set_worker_property(self, prop): pass

    @abstractmethod
    def remove_worker_property(self, prop): pass

    @abstractmethod
    def get_worker_properties(self): pass

    # Master-only
    @abstractmethod
    def restart_failed_tasks(self, file_path): pass
```

**Master 实现无操作**: Master 的 Worker 属性方法打印 WARN 并无操作。

**Worker 实现完整**: Worker 实现所有 FlyAgent 方法，并支持运行时动态修改属性。

### main.init() — 初始化入口

```python
def init(log_dir="fly_log", worker_mode=False, worker_id=0,
         master_host="127.0.0.1", master_port=0):
    # Master: setup_log_dir → init_master → configure_master → get_agent
    # Worker: init_worker → configure_worker → get_agent
```

---

## 完整使用示例

### Master 端用户脚本

```python
from fly import open_db, get_config, as_task
from fly.agent import Master

config = get_config()
config.set_int("track_writes", 1)

master = Master()
master.launch_local_workers([{"role": "hybrid"}])

db = open_db("/data/project")

@as_task(inputs=lambda db, name: [db.get_obj_name(f"input/{name}")])
def process_data(db, name):
    raw = db.read_object(f"input/{name}")
    result = algorithm(raw)
    db.write_object(f"output/{name}.result", result)

process_data(db, "file1")
process_data(db, "file2")
```

### 程序入口 (main.cpp)

```
fly user_script.py              # Master 模式
fly --worker --worker-id 1 \   # Worker 模式
    --master-host 127.0.0.1 \
    --master-port 8000
```

---

### 失败任务持久化与重启

**触发条件**:
- Capability 不匹配: task 声明 `@as_task(requires=["gpu"])` 但无 Worker 拥有 GPU
- 依赖无法解析: 仅有 pending_tasks，无 ready/running，依赖永远无法满足

**持久化流程**:
```
schedule_tasks() 检测到无法调度任务
  → 读取 bin 文件 (log_dir/failed_tasks.bin)
  → 反序列化 FailedTaskRecord 列表
  → 删除 bin 文件 (避免新 fail record 被误删)
  → 对每个 record:
      → 三阶段检查数据可用性 (try_read_local → lookup_remote_idx)
      → DataService.mark_data_ready(data_path)
      → 重新 submit_task_with_deps(task_id, ...)
  → 若仍无法调度 (如仍缺少 Worker capability) → 重新 fail 并持久化
```

**重启 API**:
```python
# 用户修复问题后（写入缺失数据、启动新 Worker）
from fly import get_master

master = get_master()
master.restart_failed_tasks("/path/to/failed_tasks.bin")
```

**依赖命名规范**:
- Task 的 inputs 必须使用 `db.get_obj_name()` 生成 full name (db_id:object_name)，与 DataService / mark_data_ready 命名空间一致

---

## 实现流程

### 任务从定义到执行

```
1. 用户脚本定义 @as_task 函数
   → func._fly_is_task = True
   → func._fly_task_name = name
   → func._fly_original_func = original_func

2. 用户调用 process_data(db, "file1")
   → wrapper(*args) 拦截
   → _serialize_args: db → "__fly_db__:db_id:base:data"
   → agent.submit(name, module, args, inputs)
   → MasterAgent.submit_task_with_deps(task_id, ...)
   → 立即返回 (异步)

3. Master 调度 → TaskAssignMessage → Worker

4. Worker executor:
   → importlib.import_module(task_module)
   → _deserialize_args: "__fly_db__:" → _Database(base, data, worker_id)
   → original_func(db, "file1")
   → 记录 writes + frozen_dbs

5. TaskCompleteMessage → Master
   → mark_data_ready → 触发下游任务
```

---

## 设计决策

| 决策 | 原因 |
|------|------|
| `_Database` 内部类 + `open_db()` 工厂 | 隐藏 C++ 实现细节，统一创建入口 |
| is_cpp 双路径序列化 | C++ 导出类型走 bitsery（高效），Python 类型走 pickle（兼容） |
| `__fly_db__:` 协议传递 Database | 轻量级 db_id 传递，Worker 端按需创建 |
| get_obj_name 自动拼 db_id | 多 DB 场景下同名对象去重 |
| _fly_original_func 保存原始函数 | Worker 端执行原始函数而非 wrapper |
| thread-local last_error_type | C++ exception 跨 nanobind 丢失类型信息 |
