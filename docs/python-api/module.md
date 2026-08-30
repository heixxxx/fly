# Python API 模块

## 模块概述

**位置**: 各模块 `py/` 目录（通过 `src/fly/` 统一导出）

- `src/fly/` — 顶层包入口（`__init__.py`, `main.py`, `runtime.py`）
- `src/agent/py/` — `agent.py`, `executor.py`
- `src/task/py/` — `task.py`
- `src/storage/py/` — `database.py`
- `src/core/py/` — `get_config()`, `Config`（合并了原 `config.py`）

Python API 层将 C++ 底层 API 包装为用户友好的高层接口，提供任务定义、Database 操作、Agent 管理和运行时配置。

### 公开符号总表（权威）

> **本表是 `from fly import ...` 全量符号的唯一权威口径**；其他文档（CLAUDE.md/AGENTS.md 等）只链接此处，不复制清单。
> 权威源码：`src/fly/__init__.py`。**新增导出必须同步本表。**

| 分组 | 符号 |
|------|------|
| Database | `open_db`, `load_db`, `merge_db`, `Database`（透传 storage） |
| db 链/uid | `generate_uid`, `make_edge`（透传 storage，详见 [db-chain-design.md](db-chain-design.md)） |
| 任务 | `as_task`, `task_name`, `wait_obj`, `wait_tasks`, `get_task_error` |
| Worker 编队 | `launch_workers`, `launch_ssh_workers`, `ensure_workers`, `wait_workers_registered`, `expect_workers` |
| 失败恢复 | `restart_failed_tasks` |
| Project | `open_project`, `load_project`, `migrate_project`, `Project`, `register_flow` |
| Agent 缓存 | `put_cache`, `get_cache`, `has_cache`, `remove_cache`, `clear_cache` |
| 消息日志 | `message`, `register_message_id`, `set_message_global_limit`, `set_message_id_limit`, `set_message_domain_limit`（详见 [message-system.md](message-system.md)） |
| MapReduce | `MapReduceJob` |
| UserDoc | `UserDoc`, `Schema`, `document`, `help`, `register_module`（详见 [userdoc.md](userdoc.md)） |
| Monitor | `launch_monitor_gui` |
| 配置/工作目录 | `get_config`, `get_work_directory` |
| 测试辅助 | `get_fly_binary` |
| 进阶 | `get_agent`（直接访问 Agent 单例） |
| 模块属性（`__getattr__` 惰性） | `completed_tasks`, `pending_tasks`, `running_tasks`, `failed_tasks`, `port` |

**不导出**：`Master`、`Worker`、`FlyAgent`（内部类）。solver 不经 `fly` re-export，用户直接 `from solver import ...`。

---

## 核心文件

| 文件 | 说明 |
|------|------|
| `__init__.py` | 顶层包，导出 open_db, as_task, launch_workers, ensure_workers, wait_tasks 等 |
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

### launch_ssh_workers(targets, ...) — SSH 启动远程 Worker

```python
def launch_ssh_workers(targets, *, ssh_port=22, ssh_user=None,
                       fly_binary=None, port=None,
                       ssh_timeout=30.0) -> list[int]:
    """
    通过 ssh 在远程主机上启动 fly worker（多机部署）。

    Args:
        targets: list of dict，每项一个 worker：
            - 'host'（必填）: ssh 目标主机（ssh 直连可达名）
            - 'attributes': 能力标签；'role': "hybrid"/"storage_only"
            - 'host_alias': 注册 hostname override（同 launch_workers 的 'host'）
        ssh_port: ssh 服务端口；ssh_user: 统一 ssh 用户名（None 用当前用户）
        fly_binary: 远端 fly 路径（None 自动探测本地路径，要求远端同路径）
        ssh_timeout: 单条 ssh 命令超时秒数（不含注册等待）

    Returns:
        分配的 worker_id list（已登记注册占位符，配合 wait_workers_registered）。

    Example:
        ids = launch_ssh_workers([
            {"host": "node1"},
            {"host": "node2", "attributes": ["highmem"], "role": "storage_only"},
        ])
        wait_workers_registered()
    """
    return get_agent().launch_ssh_workers(...)
```

**语义与边界**:
- worker 以 **nohup 后台化**在远端运行，ssh 会话立即返回；生命周期由框架消息管理
  （master stop() 广播 ShutdownMessage → worker 自杀；master 失联按心跳超时自退），
  **不持本地进程句柄**
- 注册占位符先于 ssh 下发登记（防注册竞态泄漏，同 launch_local_workers）；
  ssh 失败抛 RuntimeError，失败占位符无法回收，需终止本次 run
- **寻址**：worker 仅凭 `--config-file` 引导——master 地址/端口由 master 侧
  自动写入 `.fly_config`（首写完备 + 原子写，见 [core/module.md](core/module.md)），
  接口无任何地址参数
- **路径约定**：fly_binary / log_dir / config 文件要求 master 侧与远端一致
  （localhost 自连、共享存储下成立）；异路径部署显式传 `fly_binary` 并保证
  `log_dir` 远端可写
- 配置传递：`--config-file` 指向共享 `.fly_config`（Config 跨进程同步机制不变）
- QA：`qa/network/test_launch_ssh_workers.py`（localhost 自连环回：启动→注册→
  数据面→stop 生命周期闭环）；环境要求 sshd + 免密（配置方法见该测试文件头注）

### ensure_workers(workers, timeout=10.0, exclude=None) — 按属性申请编队

```python
def ensure_workers(workers, timeout: float = 10.0, exclude: str = None) -> bool:
    """
    向 master 申请现有 worker 并为选中 worker 追加指定属性（不启动新进程）。

    Args:
        workers: list，长度即申请的 worker 数；每个元素是该 worker 要追加的
                 属性集合——str（单属性简写）或 str 的 list。属性是追加去重。
        timeout: 空闲收集时限秒数（也是本调用全部等待的总上限）；到点放宽
                 忙碌候选。<=0 跳过阶段一直接按放宽口径收集。
        exclude: 正则字符串（re.search）；worker 任一既有属性命中即排除出
                 候选池（并发 flow 防碰撞）。

    Returns:
        True（编队就绪）。资源不足或生效超时抛 RuntimeError。

    Example:
        # 求解正式启动前确认编队（属性经 SolveDb.worker_attr 单点生成）
        ensure_workers([
            db.worker_attr("sd_0"),
            db.worker_attr("sd_1"),
            db.worker_attr("check"),
        ], timeout=10.0, exclude=r"^rasg:")
    """
    return get_agent().ensure_workers(workers, timeout=timeout, exclude=exclude)
```

**语义要点**:
- **两阶段收集**：时限内缺口只从空闲候选补齐；到点仍未齐放宽忙碌候选
  （打上属性后不等其空闲，后续 task 由调度按 requires 自动派发）
- **静态预检**：排除后的全量池（IDLE+BUSY + 已唤起未注册的占位符）盖不住
  申请数时立即抛 RuntimeError 带明细，不消耗 timeout
- **原子快照**：容量口径来自 C++ `snapshot_worker_pool`（expected 锁内
  单点采样），注册过渡态不会被漏计
- **幂等**：重复调用同规格不重复分配、不下发消息；在册池已满足时零等待
- 属性生命周期 = worker 进程生命周期（重启回 CLI 起点），需重新 ensure
- 属性命名规范：编队属性经 `SolveDb.worker_attr(tag)` = `rasg:{uid}:{tag}`
  单点生成（uid 跨进程持久），禁止手拼字符串

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

### restart_failed_tasks(dbs) — 重启失败任务

```python
def restart_failed_tasks(dbs) -> int:
    """
    重新提交之前失败的任务。

    Args:
        dbs: db / db_path 字符串 / 二者组成的 list；每个 db 目录下自动搜索
             failed_tasks.bin 并重投（读即删；重投后再失败会重新落盘）

    Returns:
        重投的 task 数

    Example:
        # 用户修复问题后（写入缺失数据、启动新 Worker）
        restart_failed_tasks(db)                    # Database 对象
        restart_failed_tasks("/data/project")       # db_path
        restart_failed_tasks([db1, "/data/proj2"])  # 混合 list
    """
    return get_agent().restart_failed_tasks(dbs)
```

> **注意**：旧的单 bin 文件路径直传形态（`restart_failed_tasks("/path/failed_tasks.bin")`）已废弃。
> 失败记录按 task 归属 db 落盘（`{owner_db_path}/failed_tasks.bin`），统一传 db 检索。
> 规范详见 [DEVELOPMENT_GUIDELINES.md §15.4](../DEVELOPMENT_GUIDELINES.md)。

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

**注意**: 此缓存与 Python ReadCache（read_object 的对象缓存）是独立的系统：
- **Agent Cache** (`put_cache/get_cache`): worker 进程级通用 Python 对象缓存——task 间传递、修改后需复用的数据（如求解器 LU 结果）
- **ReadCache** (`read_object` 的 `cache` 参数): 解压后完整对象的缓存，双池（主池 low/high 等级 + temp 池半容独立），等级只影响淘汰优先级

---

### _Database — Database 内部类

```python
class _Database:
    def __init__(self, base_path, data_path="", writer_id=0):
        # Master 模式: agent._agent.get_or_create_database(...)
        # Worker 模式: ex_stg_create_database(...)

    def write_object(self, name: str, obj, *, backup: bool = False,
                     save_to_db: bool = True, cache: str = "none") -> str:
        # 自动检测 C++ 导出类（_write_to_db）或 pickle 流式写
        # backup=True: 异步将数据副本写入另一个 Worker（跨 host），零解压压缩传输
        # save_to_db=False: temp 写（.temp.* 落盘、不注册 master 可见性，
        #   write-through 持久；freeze 时全删）
        # cache: 写后预热——"low"/"high" 将刚写的对象入缓存（正式对象→主池，
        #   temp 对象→temp 池）；"none"（默认）不缓存

    def read_object(self, name: str, *, backup: bool = False, cache: str = "low"):
        # 恒流式读取（R+常数内存）：本地 DiskChunkSource / 远程 NetworkChunkSource
        # 统一 pread 分片流；对象不可见（全源 miss）抛 KeyError
        # cache: "low"（默认，读到即入缓存）| "high"（高淘汰优先级）| "none"（零缓存）
        # 缓存语义（2026-08-30 双池裁定）：
        #   - 命中查询不分级（任一等级命中即返回同一对象引用——零拷贝）
        #   - **只读约定**：缓存对象调用方不得修改；读后需修改请显式 cache="none"
        #     （每次全新反序列化）。原地修改会污染缓存（scipy splu 案例，
        #     chunked-transfer-design §14.12），FLY_CACHE_GUARD=1 可诊断
        #   - temp 对象自动路由 temp 池（is_temp 由读取原语携带）
        #   - 等级只影响淘汰优先级：low 计分 ×low_score_factor（默认 25%），
        #     同热度下先于 high 淘汰；命中不自动升级

    def remove_object(self, name: str):
        # 删除对象索引（本地上移除，通知Master广播删除）

    def get_full_name(self, name: str) -> str:
        # 返回 "{db_path}:{name}" 唯一标识符

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
        if isinstance(arg, Database):   # Database 对象
            result.append(f"__fly_db2__:{uid}:{db_path}")
        else:
            result.append(pickle.dumps(arg).hex())
    return result
```

> **注意**: db 句柄协议串为 `__fly_db2__:{uid}:{db_path}` 两段（2026-08-26 起 uid 合入 _DB_META，
> data_path 为 db 级属性存 _DB_META，协议串不再携带）。对象全名是 `db_path:short_name`（无 db_id），
> 确保与 DataService / DependencyGraph 命名空间一致。详见 [db-chain-design.md](db-chain-design.md)。

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

    def restart_failed_tasks(self, dbs) -> int:
        # 读取失败任务记录（按归属 db 搜索 failed_tasks.bin），检查数据可用性，重新提交

    def broadcast_object_removed(self, db_path: str, object_name: str):
        # 广播对象删除给所有 Worker

    @property
    def pending_tasks / running_tasks / completed_tasks
```

### Worker — Worker Agent 封装

```python
class Worker(FlyAgent):
    def __init__(self, worker_id, master_host, master_port):
        self._agent = EXAgentWorker(worker_id, master_host, master_port)
        self._db_cache = {}            # db_path → _Database

    def start(self):
        self._executor = EXTaskExecutor()
        self._executor.set_exec_func(create_executor(self))
        self._agent.set_executor(self._executor)
        self._agent.start()

    def submit(self, name, module, args, inputs=None) -> int:
        # 递归任务提交: agent.submit_task(...)

    def get_database(self, db_path: str):
        return self._db_cache[db_path]

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
        # 6. postprocess: drain_write_back() 落盘（保证 write 触发 record_write）
        # 7. 返回 {task_id, status, output, error, outputs, frozen_dbs}
        #    注：frozen_dbs 不再由 executor 差集推断。freeze 是 task 内主动行为，
        #    Database::freeze() 执行时已即时发 DatabaseFreezeNotification 给 master
        #    登记 pending；task 完成时 master 按 task_id 提交 pending → confirmed。
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
def init():
    # 按 ProcessInfo.worker_mode 分派：configure_worker/configure_master
    # → get_agent() → agent.start()
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

restart_failed_tasks(db)              # Database 对象
restart_failed_tasks("/data/proj")    # db_path
```

**依赖命名规范**:
- Task 的 inputs 必须使用 `db.get_full_name()` 生成 full name (`db_path:short_name`)，与 DataService / mark_data_ready 命名空间一致

---

### open_db 路径检测

`open_db(path)` 检测目标路径是否已包含数据库（通过 `_DB_META` 文件判断）：

- **路径无 DB**: 直接在 `path` 创建新数据库（db 身份 = db_path + uid，见 [db-chain-design.md](db-chain-design.md)）
- **路径已有 DB**: 自动递增路径 `path.1`, `path.2`... 并打印 WARN 日志

```python
from fly import open_db

db1 = open_db("/data/project")       # 创建在 /data/project
db2 = open_db("/data/project")       # WARN: 自动创建在 /data/project.1
db3 = open_db("/data/project")       # WARN: 自动创建在 /data/project.2
```

> 历史：db_id（10-char base62）机制已废弃（ADR 0002），现行 db 身份为 db_path + uid（`_DB_META` JSON version 2）。

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
   → _serialize_args: db → "__fly_db2__:{uid}:{db_path}"
   → agent.submit(task_name, module, args, inputs)
   → MasterAgent.submit_task_with_deps(task_id, ...)
   → 立即返回 (异步)

3. Master 调度 → TaskAssignMessage → Worker

4. Worker executor:
   → from_user 模块: pickle.loads(payload) 重建函数
   → 仓库模块: importlib.import_module(task_module) + getattr
   → _deserialize_args: "__fly_db2__:" → 按 db_path 打开 db + uid 校验（目录不存在时查 master）
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
| `__fly_db2__:` 协议传递 Database | uid + db_path 轻量句柄，Worker 端按需打开（见 [db-chain-design.md](db-chain-design.md)） |
| get_full_name 拼 `db_path:short_name` | 多 DB 场景下同名对象去重 |
| _fly_original_func 保存原始函数 | Worker 端执行原始函数而非 wrapper |
| thread-local last_error_type | C++ exception 跨 nanobind 丢失类型信息 |

---

## Project — 业务流程管理对象

Project 是比 db 更高一级的管理单元，把一条业务流程的多个步骤打包，由 Project 统一管理各步骤产生的 db。**纯 Python**，零 C++。详见 [`docs/project-design.md`](../project-design.md)。

### 核心 API

```python
fly.open_project(path) -> "Project"        # 新建/绑定（基类 Project，纯机制壳）
fly.load_project(path) -> "Project"         # 全量恢复（master-only，还原真实子类）
fly.register_flow(target_cls)               # 装饰器：注册业务流程到 Project 子类
fly.Project                                 # 基类（供继承）
```

### 设计要点

- **流程注册制**：基类只提供机制（建库/取库/冻结/持久化/load），不含业务流程；每个流程 API 是普通函数，通过 `@register_flow(子类)` 注入到指定子类，实现拆分到不同模块。
- **flow 异步 4 步原则**：master 侧 flow 只做 ①检查输入 ②建库 ③提交入口 task（`@as_task`，非阻塞）④提交 freeze task（`@as_task`，inputs 依赖入口 task 写的数据），提交后**立即返回 db**。重计算在 worker task 异步执行，进度由 master 依赖图调度推进。flow **不负责 worker 池管理**——用户脚本预先唤起必要 worker。
- **freeze 作为 task**：依赖上游数据写完后由 master 调度执行，task 内 `db.freeze()` 通知 master 更新 frozen 状态。
- **flow 自己建库返回**：不暴露通用 `create_db`；每个 flow 内部 `self._create_db(name)` 建库并 `return db`。`name` = db 子目录名（actual_name）；重名 → WARN + 自动递增（`name.1`）。
- **跨流程数据依赖显式传**：flow 间不默认传数据；需用另一 db 数据时由用户显式传 db 对象（如 `solve(name, matrix_db, ...)`），该 db 作为入口 task 的 inputs 依赖源，master 自动在数据 ready 后调度。

### Project 基类方法

| 方法 | 可见性 | 说明 |
|------|--------|------|
| `_create_db(name, data_path="")` | protected | flow 内部建库；重名 WARN + 递增；返回 db |
| `_freeze_task_deps(db, depends_on)` | protected | 构造 freeze task 的 inputs（依赖对象 full_name） |
| `get_db(name, latest=False)` | public | 取 db：默认 actual_name 精确匹配；latest=True 取同名最新版 |
| `is_db_frozen(name, latest=False)` | public | 懒查询 master frozen 状态（master-only） |
| `wait_frozen(name, timeout, latest=False)` | public | 阻塞等异步 freeze 完成 |
| `freeze_db(name)` / `freeze_all()` | public | 同步冻结（非 flow 路径） |
| `list_dbs()` / `list_flows()` | public | 内省（list_dbs 返回 actual_name） |
| `load(path)` | classmethod | 读 meta + 还原子类 + 全量 load_db（master-only） |

### 使用示例（SolverProject 模板，异步）

```python
import fly
from solver import SolverProject

fly.launch_workers([{"attributes": [f"sd_{i}"]} for i in range(4)])  # 用户唤起 worker
proj = SolverProject("./my_project")
matrix_db = proj.build_matrix(name="matrix", matrix_path="poisson_n20.npz")  # 异步返回
result_db = proj.solve(name="solve", matrix_db=matrix_db, nsd=4, omega=1.0)  # 异步返回
proj.wait_frozen("solve")                  # 等求解 + freeze 完成
result = result_db.read_object("__rasg__sol")

# 跨进程恢复
proj2 = fly.load_project("./my_project")    # 自动还原为 SolverProject
proj2.get_db("matrix")                       # 取回矩阵 db
```

### 二次开发（注册自己的 flow）

```python
import fly

class MyPipeline(fly.Project):
    pass

# flows.py（独立模块）
@fly.register_flow(MyPipeline)
def my_flow(self, name, **kw):
    db = self._create_db(name)
    # ... 业务逻辑 ...
    return db
```
