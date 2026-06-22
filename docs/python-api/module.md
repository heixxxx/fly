# Python API 模块

## 模块概述

**位置**: 各模块 `py/` 目录（通过 `src/fly/` 统一导出）

- `src/fly/` — 顶层包入口（`__init__.py`, `main.py`, `runtime.py`）
- `src/agent/py/` — `agent.py`, `executor.py`
- `src/task/py/` — `task.py`
- `src/storage/py/` — `database.py`
- `src/core/py/` — `get_config()`, `Config`（合并了原 `config.py`）

Python API 层将 C++ 底层 API 包装为用户友好的高层接口，提供任务定义、Database 操作、Agent 管理和运行时配置。

---

## 核心文件

| 文件 | 说明 |
|------|------|
| `__init__.py` | 顶层包，导出 open_db, as_task, launch_workers, wait_tasks 等 |
| `agent.py` | Master/Worker 内部实现（位于 `src/agent/py/`） |
| `database.py` | _Database 类（位于 `src/storage/py/`） |
| `task.py` | @as_task 和 @task_name 装饰器（位于 `src/task/py/`） |
| `executor.py` | Worker 任务执行器（位于 `src/agent/py/`） |
| `runtime.py` | 运行时配置（master/worker mode，内部模块） |
| `main.py` | 初始化入口 |
| `read_cache.py` | Python 侧 high 层读缓存（pickle 对象）；low 层（压缩字节）已下沉 C++ ObjectCache |

---

## 类/函数详细说明

### open_db(path, data_path) — Database 工厂

```python
def open_db(path: str, data_path: str = "") -> _Database:
    return _Database(path, data_path)
```

唯一公开的 Database 创建接口。

### launch_workers(configs) — 启动 Worker

```python
def launch_workers(configs: list[dict]) -> None:
    """
    启动本地 Worker 进程（非阻塞，立即返回）。

    Args:
        configs: Worker 配置列表，每个配置是 dict，至少包含 'role' 键。
                 可选键: 'role' (hybrid/storage_only), 'host' (覆盖 hostname)

    Example:
        launch_workers([
            {"role": "hybrid"},
            {"role": "storage_only"},
        ])
    """
    get_agent().launch_local_workers(configs)
```

**实现细节**:
- 始终使用 **process 模式**（子进程 Worker，独立 DataService 单例）
- thread 模式已移除
- Worker 进程通过 TCP 连接 Master，实现真正的进程隔离
- 内部调用 `_spawn_process_worker()` 启动子进程

### wait_tasks(timeout) — 等待任务完成

```python
def wait_tasks(timeout: float = 30.0) -> bool:
    """
    阻塞等待所有任务完成或超时。

    Args:
        timeout: 超时时间（秒），默认 30 秒

    Returns:
        True: 所有任务已完成
        False: 超时，仍有任务未完成

    Example:
        if wait_tasks(timeout=60.0):
            print("All tasks completed!")
        else:
            print("Timeout, some tasks still pending")
    """
    return get_agent().wait_for_all_tasks(timeout)
```

**实现细节**:
- 内部轮询 `agent.get_pending_tasks()` 和 `agent.get_running_tasks()`
- 每 100ms 检查一次
- 超时后返回 False，不抛异常

### load_db(path) — 恢复数据库

```python
def load_db(path: str) -> _Database:
    """
    恢复已有数据库（Master 节点专用）。

    Args:
        path: 数据库路径（包含 _DB_META 文件）

    Returns:
        恢复的 _Database 实例

    Example:
        db = load_db("/data/project")  # 恢复 DB，自动加载索引
    """
    return get_agent().load_db(path)
```

### restart_failed_tasks(path) — 重启失败任务

```python
def restart_failed_tasks(path: str) -> None:
    """
    重新提交之前失败的任务。

    Args:
        path: 失败任务记录文件路径（log_dir/failed_tasks.bin）

    Example:
        # 用户修复问题后（写入缺失数据、启动新 Worker）
        restart_failed_tasks("/path/to/failed_tasks.bin")
    """
    get_agent().restart_failed_tasks(path)
```

### 进程级跨 Task 缓存

Fly 提供进程级的通用缓存系统，用于在同一进程的不同 task 之间传递数据，无需网络/磁盘 I/O。

```python
# 存储缓存
put_cache(key: str, value: Any) -> None

# 读取缓存
get_cache(key: str, default=None) -> Any

# 检查缓存是否存在
has_cache(key: str) -> bool

# 删除单个缓存条目
remove_cache(key: str) -> None
# Raises: KeyError if key not found

# 清空所有缓存
clear_cache() -> None
```

**特性**:
- **进程级生命周期**: 缓存在 Agent 进程（Master 或 Worker）的整个生命周期内有效
- **本地隔离**: 严格本地存储，不跨 Worker 共享
- **跨 Task 共享**: 同一 Worker 上的不同 task 可以通过缓存共享数据
- **任意 Python 对象**: 值可以是任意 Python 对象

**使用场景**:
```python
@as_task(inputs=lambda db, name: [f"input/{name}"])
def task_a(db, name):
    result = expensive_computation(name)
    put_cache(f"result_{name}", result)  # 缓存中间结果

@as_task(inputs=lambda db, name: [f"output/{name}"])
def task_b(db, name):
    cached = get_cache(f"result_{name}")  # 读取缓存，避免重复计算
    if cached is None:
        cached = expensive_computation(name)
    db.write_object(f"output/{name}", cached)
```

**注意**: 此缓存与 C++ ObjectCache（read_object 的两层 LRU 缓存）是独立的系统：
- **Agent Cache** (`put_cache/get_cache`): 通用 Python 对象缓存，用于 task 间数据传递
- **ObjectCache** (`read_object` 的 `cache` 参数): 专门用于加速数据读取的两层 LRU 缓存

---

### _Database — Database 内部类

```python
class _Database:
    def __init__(self, base_path, data_path="", writer_id=0):
        # Master 模式: agent._agent.get_or_create_database(...)
        # Worker 模式: ex_stg_create_database(...)

    def write_object(self, name: str, obj, *, backup: bool = False, save_to_db: bool = True) -> str:
        # 自动检测 is_cpp → __getstate__() 或 pickle.dumps
        # backup=True: 异步将数据副本写入另一个 Worker（跨 host），零解压压缩传输
        # save_to_db=False: 写入 TempStore（独立后台线程），不落盘到 DB 目录
        #   read_object 透明读取，remove_object 清理，run 结束自动清理

    def read_object(self, name: str, *, backup: bool = False, cache: str = "low"):
        # 三层降级读取
        # Layer 1: DataService.try_read_local → 本地
        # Layer 2: lookup_remote_idx → DataClient 直连
        # Layer 3: request_remote_data → 全程远程 (最多 3 次重试)
        # backup=True: 从远程 Worker 读取压缩数据，直接落盘本地（零解压），返回解压后数据
        # cache: "low" (默认) | "high" | "none"
        # 缓存分层:
        #   - low 层（压缩字节）+ nanobind 类 high 层（反序列化对象）在 C++ ObjectCache
        #   - pickle 对象 high 层在 Python ReadCache（src/storage/py/read_cache.py）
        #   - nanobind 类（FLY_EXPORT_SERIALIZE）经 _read_from_db 走 C++ high 层（省反序列化）
        #   "low"  — 缓存压缩数据，避免重复网络/磁盘 IO
        #   "high" — 缓存反序列化后的 Python 对象，避免重复反序列化
        #   "none" — 不缓存，不从缓存读取

    def remove_object(self, name: str):
        # 删除对象索引（本地上移除，通知Master广播删除）

    def get_full_name(self, name: str) -> str:
        # 返回 "{db_id}:{name}" 唯一标识符

    def set_var(self, name: str, value):
        # 存储小对象（同步等待 master 确认）。var 不可变：同名再次 set 会被拒绝。
        # 序列化后 > 1K 打印警告（建议改用 write_object）。
    def get_var(self, name: str):
        # 读取小对象（本地缓存 miss 时回源 master）。不存在返回 None。
    def remove_var(self, name: str):
        # 删除小对象（异步，立即清本地缓存，通知 master 广播删除）。

    def get_db_id(self) -> str
    def freeze(self)
    def is_frozen(self) -> bool
    def write_object_raw(self, name, data, *, backup: bool = False) -> str
    def read_object_raw(self, name, *, backup: bool = False) -> str
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

### as_task(inputs, requires) — 任务装饰器

```python
def as_task(inputs=None, requires=None):
    def decorator(func):
        name = getattr(func, '_fly_task_name', None) or func.__name__
        module = func.__module__ or "__main__"

        if module == "__main__":
            module = "from_user"
            func_payload = "__user_func__:" + pickle.dumps(func).hex()
        else:
            func_payload = None
            _task_registry[(module, name)] = func

        def wrapper(*args, **kwargs):
            agent = get_agent()
            task_inputs = inputs(*args, **kwargs) if inputs else []
            # requires 支持 list / tuple(list, float) / callable，解析为
            # (caps, attribute_timeout) 传递给 submit。
            resolved = requires(*args, **kwargs) if callable(requires) else requires
            if isinstance(resolved, tuple) and len(resolved) == 2:
                caps, attr_timeout = resolved
            else:
                caps, attr_timeout = list(resolved), -1.0
            serialized = _serialize_args(args)
            task_name = func_payload if func_payload is not None else name
            agent.submit(task_name, module, serialized, task_inputs,
                         required_capabilities=caps,
                         attribute_timeout=attr_timeout)

        wrapper._fly_original_func = func
        wrapper._fly_task_name = name
        return wrapper
    return decorator
```

**requires 参数形式**（属性依赖 + 超时语义）:

- `list[str]`：能力标签列表，死等（必须满足才调度）。
- `tuple(list[str], float)`：`(能力标签列表, 属性依赖超时秒数)`：
    - `timeout < 0`：死等（等价纯 list）。
    - `timeout == 0`：数据依赖满足后仅检查一次，无完整匹配立即降级到匹配属性最多的 idle worker。
    - `timeout > 0`：数据依赖满足后限时等待；到期后降级调度。
- `callable(*args, **kwargs)`：返回上述任一形式，在提交时动态解析。

```python
@as_task(requires=["gpu"])                  # 死等 gpu
@as_task(requires=(["gpu"], 5.0))           # 5 秒后降级
@as_task(requires=lambda db, k: ([k], 1.0)) # 动态决定
```

**vars 参数形式**（小对象预取）:

声明 task 需要的 var 变量，master 在调度时将已存在的 var 数据 inline 进
`TaskAssignMessage` 一次性发给 worker，worker 执行前注入本地缓存，避免额外网络往返。

- `list[str]`：var 全名列表（`db.get_full_name(name)` 生成）。
- `callable(*args, **kwargs) -> list[str]`：提交时动态解析。

var 不存在仅打印 WARN，不影响调度。var 的真实数据依赖靠 `write_object`
隐式确定（同连接 FIFO 保证顺序）。

```python
@as_task(vars=lambda db: [db.get_full_name("counter")])  # 声明需要 counter var
def read_counter(db):
    return db.get_var("counter")
```

**用户脚本 vs 仓库模块**:
- `__main__`（用户脚本）：函数被 pickle 序列化到 task_name 字段，Worker 通过反序列化重建函数
- 仓库模块：函数注册到 `_task_registry`，Worker 通过 `importlib.import_module` 加载

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

> **注意**: inputs 使用 `db.get_full_name()` 生成 full name（`db_id:object_name`），确保与 DataService / DependencyGraph 命名空间一致。

```python
# 正确
@as_task(inputs=lambda db, key: [db.get_full_name(f"input/{key}")])

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
        # init() 时自动调用，用户无需手动启动
        # launch_local_workers() 和 submit() 也会幂等调用
        self._agent.start()
        self._port = self._agent.get_port()

    def submit(self, name, module, args, inputs=None) -> int:
        # 线程安全 task_counter
        # agent.submit_task_with_deps(task_id, ...)

    def launch_local_workers(self, worker_configs, port=None):
        # 始终使用子进程 Worker (_spawn_process_worker)
        # Worker 进程通过 TCP 连接 Master，实现真正的进程隔离

    def stop(self):
        # 停止所有 Worker → 停止 Agent

    def restart_failed_tasks(self, file_path: str):
        # 读取失败任务记录，检查数据可用性，重新提交

    def broadcast_object_removed(self, db_id: str, object_name: str):
        # 广播对象删除给所有 Worker

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

### runtime — 运行时配置（内部模块）

```python
_mode: str = "master"     # "master" or "worker"
_agent: FlyAgent = None   # 全局 Agent 实例

def get_agent() -> FlyAgent:       # 懒初始化 Agent（导出给进阶用户）
def configure_worker(...)          # 设置 Worker 模式（内部）
def configure_master(...)          # 设置 Master 模式（内部）
def reset()                        # 重置 Agent（内部）
```

**FlyAgent 抽象基类**（内部，不导出给用户）:
```python
from fly import get_agent

agent = get_agent()  # 返回 Master 或 Worker 单例

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

    # load_db 恢复
    @abstractmethod
    def load_db(self, path): pass

    @abstractmethod
    def wait_for_all_workers(self, count, timeout=30): pass
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
from fly import open_db, get_config, as_task, launch_workers, wait_tasks

config = get_config()
config.set_int("track_writes", 1)

launch_workers([{"role": "hybrid"}])

db = open_db("/data/project")

@as_task(inputs=lambda db, name: [db.get_full_name(f"input/{name}")])
def process_data(db, name):
    raw = db.read_object(f"input/{name}")
    result = algorithm(raw)
    db.write_object(f"output/{name}.result", result)

process_data(db, "file1")
process_data(db, "file2")

wait_tasks()
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
from fly import restart_failed_tasks

restart_failed_tasks("/path/to/failed_tasks.bin")
```

**依赖命名规范**:
- Task 的 inputs 必须使用 `db.get_full_name()` 生成 full name (db_id:object_name)，与 DataService / mark_data_ready 命名空间一致

---

### open_db 路径检测

`open_db(path)` 检测目标路径是否已包含数据库（通过 `_DB_META` 文件判断）：

- **路径无 DB**: 直接在 `path` 创建新数据库，db_id 为 10-char base62（4 char path-hash 前缀 + 6 char 随机后缀）
- **路径已有 DB**: 自动递增路径 `path.1`, `path.2`... 并打印 WARN 日志

```python
from fly import open_db

db1 = open_db("/data/project")       # 创建在 /data/project
db2 = open_db("/data/project")       # WARN: 自动创建在 /data/project.1
db3 = open_db("/data/project")       # WARN: 自动创建在 /data/project.2
```

**db_id 生成**: `<4-char path-hash><6-char random>` = 10-char base62。
- **前缀**：base_path 的 FNV-1a 32-bit hash 映射到 4 个 base62 字符（同路径 → 同前缀）。
- **后缀**：6 个随机 base62 字符（~35.7 bit 熵）。
- **碰撞检测**：生成时若 id 已被 `DataService` 注册（如路径迁移后 load 了旧 db，又在原路径新建），重抽随机后缀重试。

`load_db` 从 `_DB_META` 读回原 db_id（不重新生成），故 DB 迁移后 id 不变。

---

### load_db 数据库恢复

**场景**: Master 进程重启后，恢复之前创建的 Database 及其数据索引。

```python
from fly import load_db

# Run 1: 创建 DB，写入数据
from fly import open_db, launch_workers
db = open_db("/data/project")
launch_workers([{}])
# ... 写入数据，执行任务 ...

# Run 2: 重启后恢复 DB
db = load_db("/data/project")   # 恢复 DB，自动加载索引，启动 Worker
# ... db_id 不变，索引已恢复，可继续读取数据 ...
```

**DB 移动支持**: `_DB_META` 仅存储 `db_id`（不含 base_path），因此 DB 可被移动到新路径后恢复：
```python
# DB 被移动
master.load_db("/new/location/project")  # db_id 从 _DB_META 读取，不受路径变化影响
```

**关键行为**:
- `load_db` 内部使用 process worker
- `next_worker_id` 从旧记录中推断，避免 idx 文件名冲突
- Worker 的 `on_idx_load_command` 注册 `db_paths_` 并恢复 entries

---

## 实现流程

### 任务从定义到执行

```
1. 用户脚本定义 @as_task 函数
   → __main__ 模块: pickle.dumps(func).hex() 存入 task_name
   → 仓库模块: 注册到 _task_registry[(module, name)]

2. 用户调用 process_data(db, "file1")
   → wrapper(*args) 拦截
   → _serialize_args: db → "__fly_db__:db_id:base:data"
   → agent.submit(task_name, module, args, inputs)
   → MasterAgent.submit_task_with_deps(task_id, ...)
   → 立即返回 (异步)

3. Master 调度 → TaskAssignMessage → Worker

4. Worker executor:
   → from_user 模块: pickle.loads(payload) 重建函数
   → 仓库模块: importlib.import_module(task_module) + getattr
   → _deserialize_args: "__fly_db__:" → 检查 DataService.has_database(db_id)
   → executor 执行完成后 drain_write_back() 确保数据落盘
   → original_func(db, "file1")
   → 记录 writes + frozen_dbs

5. TaskCompleteMessage → Master
   → mark_data_ready → 触发下游任务
```

---

## 设计决策

| 决策 | 原因 |
|------|------|
| 函数级 API（launch_workers, wait_tasks 等） | 隐藏 Master/Worker 内部实现，用户无需直接构造 Agent |
| `_Database` 内部类 + `open_db()` 工厂 | 隐藏 C++ 实现细节，统一创建入口 |
| is_cpp 双路径序列化 | C++ 导出类型走 bitsery（高效），Python 类型走 pickle（兼容） |
| `__fly_db__:` 协议传递 Database | 轻量级 db_id 传递，Worker 端按需创建 |
| get_full_name 自动拼 db_id | 多 DB 场景下同名对象去重 |
| _fly_original_func 保存原始函数 | Worker 端执行原始函数而非 wrapper |
| thread-local last_error_type | C++ exception 跨 nanobind 丢失类型信息 |
