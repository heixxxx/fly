# Layer 5: Python 高层 API + 写入跟踪设计

**日期**: 2026-05-16
**状态**: 设计阶段

---

## 一、设计目标

将当前 C++ 底层 API 包装为用户友好的 Python 高层 API，实现设计文档 24.3 章的使用方式：

```python
# user_tasks.py - 目标用户体验
from fly import master, Database, get_config
from fly.task import as_task, task_name

config = get_config()
config.set(heartbeat_timeout=120, track_writes=1)

master.launch_local_workers([{"role": "hybrid"}])

@as_task(inputs=lambda db, name: [f"input/{name}"])
def process_data(db, name):
    raw = db.read_object(f"input/{name}")
    result = algorithm(raw)
    db.write_object(f"output/{name}.result", result)  # 自动跟踪写入

process_data(db, "file1")
```

---

## 二、写入跟踪机制修正

### 2.1 原设计问题

原设计考虑：
- `config.track_writes=1` 启用全局写入跟踪
- Database 记录 `written_objects_`

**问题**：全局配置不适用于多 Database 场景

### 2.2 修正方案

**核心原则**：
1. **写入跟踪由 Worker Agent 管理**：执行任务时，Worker Agent 维护当前任务的写入列表
2. **Database 调用 Agent API**：`db.write_object()` 时调用 Agent 的记录接口
3. **多 db 支持**：写入对象名称拼接 `db_id` 作为唯一标识符

### 2.3 数据流设计

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    写入跟踪数据流                                          │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  [任务执行开始]                                                           │
│  WorkerAgent.begin_task(task_id)                                       │
│      │                                                                  │
│      ▼                                                                  │
│  [用户代码执行]                                                           │
│  db.write_object("output/result", data)                                │
│      │                                                                  │
│      ▼ [Database 调用 Agent API]                                        │
│  WorkerAgent.record_write(db_id, object_name)                          │
│      │                                                                  │
│      ▼ [记录格式]                                                        │
│  write_record = {                                                       │
│      db_id: "db_abc123",           // Database唯一标识                   │
│      object_name: "output/result", // 用户指定的对象名                    │
│      full_name: "db_abc123:output/result"  // 拼接后的唯一标识           │
│  }                                                                      │
│      │                                                                  │
│      ▼ [存储到 task_writes_]                                             │
│  WorkerAgent.task_writes_[task_id].push_back(write_record)             │
│                                                                         │
│  [任务执行结束]                                                           │
│  WorkerAgent.end_task(task_id)                                         │
│      │                                                                  │
│      ▼ [发送 TaskCompleteMessage]                                       │
│  TaskCompleteMessage.written_objects = [                               │
│      "db_abc123:output/result",                                        │
│      "db_abc123:output/intermediate"                                   │
│  ]                                                                      │
│      │                                                                  │
│      ▼ [Master 标记数据就绪]                                             │
│  MasterAgent.on_task_complete(msg)                                     │
│      for path in msg.written_objects:                                  │
│          graph_->mark_data_ready(path)                                 │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### 2.4 API 设计

#### WorkerAgent C++ 接口

```cpp
// src/agent/cpp/worker_agent.h
class WorkerAgent {
public:
    void begin_task(uint64_t task_id);
    void record_write(const CMString& db_id, const CMString& object_name);
    CMVector<CMString> end_task(uint64_t task_id);  // 返回写入列表
    
private:
    CMMap<uint64_t, CMVector<CMString>> task_writes_;  // task_id → 写入列表
    uint64_t current_task_id_;  // 当前执行的任务
};
```

#### Database 调用 Agent

```cpp
// src/storage/cpp/database.h
class Database {
public:
    CMString write_object(const CMString& name, ...) {
        CMString result = writer_->write_object(name, ...);
        
        // 调用 Worker Agent 记录写入（如果在任务执行上下文中）
        if (auto* agent = get_current_worker_agent()) {
            agent->record_write(db_id_, name);
        }
        
        return result;
    }
    
private:
    CMString db_id_;  // Database 唯一标识符
};
```

#### Worker Agent 获取机制

```cpp
// src/storage/cpp/database.cpp
WorkerAgent* get_current_worker_agent() {
    // 从全局上下文获取当前 Worker Agent
    // 仅在 Worker 任务执行线程中有效
    return WorkerAgentContext::current();
}

// src/agent/cpp/worker_agent_context.h
class WorkerAgentContext {
public:
    static WorkerAgent* current();  // 获取当前执行上下文的 Agent
    static void set(WorkerAgent* agent);  // 设置执行上下文
};
```

### 2.5 唯一标识符设计

**问题**：多 db 可能写入同名对象，如何区分？

**方案**：拼接 `db_id` 前缀

```
写入对象唯一标识格式："{db_id}:{object_name}"

示例：
- Database db_a (db_id="proj_a") 写入 "output/result"
  → 唯一标识: "proj_a:output/result"
  
- Database db_b (db_id="proj_b") 写入 "output/result"
  → 唯一标识: "proj_b:output/result"

依赖声明时使用完整标识：
@as_task(inputs=lambda db, name: [f"{db.get_db_id()}:input/{name}"])
```

### 2.6 Python 导出

```python
# src/fly/database.py
class Database(_CDatabase):
    def write_object(self, name: str, obj) -> str:
        result = self._write_typed(name, ...)
        # C++ 内部自动调用 Agent.record_write(db_id, name)
        return result
    
    def get_db_id(self) -> str:
        return self._db_id  # 返回唯一标识符

# src/fly/task.py
def as_task(inputs=None):
    def decorator(func):
        def wrapper(*args, **kwargs):
            # 计算依赖时需要考虑 db_id
            # inputs 函数签名：inputs(db, *args) 
            deps = inputs(*args, **kwargs) if inputs else []
            # deps 应为完整标识符 ["db_id:input/name", ...]
            ...
        return wrapper
    return decorator
```

---

## 三、任务结构设计

### 3.1 任务参数传递

当前：手动传递 args 数组

设计目标：自动序列化函数参数

```python
@as_task(inputs=lambda db, name: [f"{db.get_db_id()}:input/{name}"])
def process_data(db, name):
    raw = db.read_object(f"input/{name}")
    ...

process_data(db_a, "file1")
# 自动序列化：args = pickle.dumps((db_a, "file1"))
```

**关键设计**：
- `db` 参数传递 Database 对象引用（或 db_id）
- Worker 端重建 Database 对象或通过 db_id 获取

### 3.2 Worker 执行流程

```
1. Worker 收到 TaskAssignMessage
2. import task_module（加载用户定义）
3. 获取 task_func = getattr(module, task_name)
4. args = pickle.loads(serialized_args)
5. WorkerAgent.begin_task(task_id)
6. WorkerAgentContext.set(agent)  // 设置执行上下文
7. 执行 task_func._fly_original_func(*args)
8. WorkerAgentContext.set(None)
9. writes = WorkerAgent.end_task(task_id)
10. 发送 TaskCompleteMessage(written_objects=writes)
```

---

## 四、实现任务清单

### Phase 1: 写入跟踪核心机制

| Task | 内容 | 预估工作量 |
|------|------|-----------|
| 1.1 | WorkerAgent 添加 begin_task/end_task/record_write 接口 | 中 |
| 1.2 | WorkerAgentContext 全局上下文管理 | 中 |
| 1.3 | Database.write_object 调用 Agent.record_write | 中 |
| 1.4 | Database.db_id 生成和存储 | 低 |
| 1.5 | TaskCompleteMessage 使用完整标识符格式 | 低 |
| 1.6 | Python 导出 db_id 和 write_object 自动跟踪 | 中 |
| 1.7 | 测试：多 db 同名对象写入 | 中 |

### Phase 2: Python 高层 API

| Task | 内容 | 预估工作量 |
|------|------|-----------|
| 2.1 | fly/__init__.py 顶层包 | 低 |
| 2.2 | fly/task.py @as_task 装饰器 | 中 |
| 2.3 | fly/task.py @task_name 装饰器 | 低 |
| 2.4 | fly/master.py Master 类包装 | 中 |
| 2.5 | fly/master.py launch_local_workers() | 中 |
| 2.6 | fly/config.py Config 包装 | 低 |
| 2.7 | 测试：完整用户代码流程 | 中 |

### Phase 3: Worker 自动执行

| Task | 内容 | 预估工作量 |
|------|------|-----------|
| 3.1 | Worker Agent 自动 import module | 中 |
| 3.2 | Worker Agent pickle args 反序列化 | 中 |
| 3.3 | Worker Agent 执行原始函数 | 中 |
| 3.4 | fly-worker 启动脚本 | 中 |
| 3.5 | fly 主命令行入口 | 中 |
| 3.6 | 测试：端到端用户脚本执行 | 高 |

### Phase 4: 数据定位（可选）

| Task | 内容 | 预估工作量 |
|------|------|-----------|
| 4.1 | Worker Data Server 线程池 | 高 |
| 4.2 | Master data_location_ 管理 | 中 |
| 4.3 | DataQueryMessage/DataLocationMessage | 中 |
| 4.4 | Database.read_object 远程读取 | 高 |
| 4.5 | 测试：跨 Worker 数据读取 | 高 |

---

## 五、关键设计决策

### 5.1 db_id 生成策略

**选项 A**：用户指定
```python
db = Database("/data", db_id="my_project")
```

**选项 B**：自动生成（基于路径哈希）
```python
db = Database("/data/project_a")
# db_id = hash("/data/project_a") → "proj_a_hash123"
```

**推荐**：选项 B（自动生成），避免用户手动管理冲突

### 5.2 Database 对象传递策略

**选项 A**：传递 db_id 字符串
```python
# Worker 端通过 db_id 重建 Database
db = StorageManager.get_database(db_id)
```

**选项 B**：序列化完整 Database 对象
```python
# pickle.dumps(db) → 包含 base_path, data_path
```

**推荐**：选项 A（传递 db_id），更轻量且避免序列化问题

### 5.3 依赖声明格式

```python
@as_task(inputs=lambda db, name: [f"{db.get_db_id()}:input/{name}"])
def process_data(db, name):
    # db.read_object 自动解析 db_id，优先本地读取
    raw = db.read_object(f"input/{name}")
```

---

## 六、测试场景

### 6.1 写入跟踪基础测试

```python
def test_write_tracking():
    agent = WorkerAgent(1, "127.0.0.1", 19090)
    agent.begin_task(1)
    
    db = Database("/data/test")
    db.write_object("output/a", "data1")
    db.write_object("output/b", "data2")
    
    writes = agent.end_task(1)
    assert writes == ["{db_id}:output/a", "{db_id}:output/b"]
```

### 6.2 多 db 同名对象测试

```python
def test_multi_db_same_name():
    agent = WorkerAgent(1, ...)
    agent.begin_task(1)
    
    db_a = Database("/data/proj_a")
    db_b = Database("/data/proj_b")
    
    db_a.write_object("output/result", "from_a")
    db_b.write_object("output/result", "from_b")
    
    writes = agent.end_task(1)
    assert writes == [
        "{db_a_id}:output/result",
        "{db_b_id}:output/result"
    ]
```

### 6.3 依赖调度测试

```python
def test_dependency_with_db_id():
    master.submit_task_with_deps(
        task_id=2,
        name="aggregate",
        module="tasks",
        args=[...],
        inputs=["{db_a_id}:output/a"],  # 依赖 db_a 的输出
        outputs=[]
    )
```

---

## 七、与设计文档对齐

实现 Phase 1-3 后，用户代码可达到设计文档 24.3 章的体验：

```python
from fly import master, Database, get_config
from fly.task import as_task

config = get_config()
config.set(track_writes=1)  # 启用写入跟踪

master.launch_local_workers([{"role": "hybrid"}])

@as_task(inputs=lambda db, name: [f"{db.get_db_id()}:input/{name}"])
def process_data(db, name):
    raw = db.read_object(f"input/{name}")
    result = algorithm(raw)
    db.write_object(f"output/{name}.result", result)  # 自动跟踪

db = Database("/data/project")
process_data(db, "file1")  # 一行提交任务
```

---

## 八、约束与偏好

- 后续流程使用中文进行回复
- 永远不要直接使用 `bazel build` 或 `bazel test` 命令，必须使用 `./fly.sh` 脚本
- 项目使用 gcc12 编译器
- C++20 标准
- TDD approach: write failing test, implement, pass, commit