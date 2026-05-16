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

@as_task(inputs=lambda db, name: [db.get_obj_name(f"input/{name}")])
@task_name("processor")
def process_data(db, name):
    # read_object 内部自动使用 get_obj_name 查找数据
    raw = db.read_object(f"input/{name}")
    result = algorithm(raw)
    # write_object 自动跟踪写入，无需手动声明 outputs
    db.write_object(f"output/{name}.result", result)

# 创建 Database
db_a = Database("/data/project_a")
db_b = Database("/data/project_b", data_path="/ssd/local_b")

# 提交任务（一行代码）
process_data(db_a, "file1")
process_data(db_b, "file2")

# 冻结任务作为最后一步
@as_task(inputs=lambda db, deps: [db.get_obj_name(f"output/{name}.result") for name in deps])
def freeze_db(db, deps):
    db.freeze()

freeze_db(db_a, ["file1"])
freeze_db(db_b, ["file2"])
```

**关键简化**：
- `db.get_obj_name("key")` 返回唯一标识符，无需手动拼接 db_id
- `db.read_object("key")` 内部自动处理 db_id
- `db.write_object("key")` 自动跟踪写入

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
    Database(const CMString& base_path, const CMString& data_path = "");
    
    // 获取对象唯一标识符
    CMString get_obj_name(const CMString& name) const {
        return db_id_ + ":" + name;
    }
    
    // 写入对象（自动跟踪）
    template<typename T>
    CMString write_object(const CMString& name, const T& obj, ...) {
        CMString result = writer_->write_object(name, obj, ...);
        
        // 自动调用 Worker Agent 记录写入
        if (auto* agent = WorkerAgentContext::current()) {
            agent->record_write(db_id_, name);
        }
        
        return result;
    }
    
    // 读取对象（自动定位）
    template<typename T>
    std::shared_ptr<T> read_object(const CMString& name) {
        // 使用 get_obj_name 作为查找 key
        CMString full_name = get_obj_name(name);
        
        // 优先本地读取
        if (has_local(full_name)) {
            return reader_->read_object<T>(name);
        }
        
        // 否则请求远程数据
        return request_remote(full_name);
    }
    
    CMString get_db_id() const { return db_id_; }
    
private:
    CMString db_id_;  // Database 唯一标识符
    CMString base_path_;
    CMString data_path_;
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

**方案**：Database 提供 `get_obj_name()` 方法自动拼接 db_id

```python
# 用户使用方式（简洁）
@as_task(inputs=lambda db, name: [db.get_obj_name(f"input/{name}")])
def process_data(db, name):
    raw = db.read_object(f"input/{name}")  # read_object 内部自动处理
    result = algorithm(raw)
    db.write_object(f"output/{name}.result", result)  # write_object 自动跟踪

# get_obj_name 返回唯一标识符
db.get_obj_name("input/file1") → "{db_id}:input/file1"
```

**内部实现**：
```
写入对象唯一标识格式："{db_id}:{object_name}"

示例：
- Database db_a.get_obj_name("output/result") → "proj_a:output/result"
- Database db_b.get_obj_name("output/result") → "proj_b:output/result"

依赖声明简化：
@as_task(inputs=lambda db, name: [db.get_obj_name(f"input/{name}")])
# 无需手动拼接 db_id
```

### 2.6 Python 导出

```python
# src/fly/database.py
class Database(_CDatabase):
    def write_object(self, name: str, obj) -> str:
        # C++ 内部自动调用 Agent.record_write(db_id, name)
        return self._write_typed(name, ...)
    
    def read_object(self, name: str):
        # 自动查找数据位置（本地优先，远程其次）
        # 内部使用 get_obj_name(name) 作为查找 key
        return self._read_typed(name)
    
    def get_obj_name(self, name: str) -> str:
        # 返回唯一标识符："{db_id}:{name}"
        return self._get_obj_name(name)
    
    def get_db_id(self) -> str:
        return self._db_id

# src/fly/task.py
def as_task(inputs=None):
    def decorator(func):
        def wrapper(*args, **kwargs):
            # 计算依赖 - 使用 db.get_obj_name() 简化
            deps = inputs(*args, **kwargs) if inputs else []
            # deps 格式：["{db_id}:input/name", ...]
            ...
        return wrapper
    return decorator
```

**用户代码示例**：
```python
@as_task(inputs=lambda db, name: [db.get_obj_name(f"input/{name}")])
def process_data(db, name):
    # read_object 无需调用 get_obj_name，内部自动处理
    raw = db.read_object(f"input/{name}")
    result = algorithm(raw)
    # write_object 自动跟踪写入，使用 get_obj_name(name) 作为记录 key
    db.write_object(f"output/{name}.result", result)
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
@as_task(inputs=lambda db, name: [db.get_obj_name(f"input/{name}")])
def process_data(db, name):
    # read_object 无需 get_obj_name，内部自动处理
    raw = db.read_object(f"input/{name}")
    
    # write_object 自动跟踪，内部使用 get_obj_name 记录
    db.write_object(f"output/{name}.result", result)
```

**简化点**：
- `inputs` 声明使用 `db.get_obj_name()` 获取唯一标识符
- `read_object()` 和 `write_object()` 内部自动处理 db_id
- 用户无需关心 db_id 拼接细节

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
    # writes 使用 get_obj_name 格式
    expected_a = db.get_obj_name("output/a")
    expected_b = db.get_obj_name("output/b")
    assert writes == [expected_a, expected_b]
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
    # 同名对象但唯一标识不同
    assert db_a.get_obj_name("output/result") in writes
    assert db_b.get_obj_name("output/result") in writes
```

### 6.3 依赖调度测试

```python
def test_dependency_with_db_id():
    db = Database("/data/project")
    
    # 使用 get_obj_name 声明依赖
    deps = [db.get_obj_name("input/file1")]
    
    master.submit_task_with_deps(
        task_id=2,
        name="process",
        module="tasks",
        args=[...],
        inputs=deps,
        outputs=[]
    )
```

### 6.4 get_obj_name 接口测试

```python
def test_get_obj_name():
    db_a = Database("/data/proj_a")
    db_b = Database("/data/proj_b")
    
    # 不同 db，同名对象，唯一标识不同
    assert db_a.get_obj_name("output/result") == f"{db_a.get_db_id()}:output/result"
    assert db_b.get_obj_name("output/result") == f"{db_b.get_db_id()}:output/result"
    
    # 唯一标识不相等
    assert db_a.get_obj_name("output/result") != db_b.get_obj_name("output/result")
```

---

## 七、与设计文档对齐

实现 Phase 1-3 后，用户代码可达到设计文档 24.3 章的体验：

```python
from fly import master, Database, get_config
from fly.task import as_task, task_name

config = get_config()
config.set(track_writes=1)

master.launch_local_workers([{"role": "hybrid"}])

@as_task(inputs=lambda db, name: [db.get_obj_name(f"input/{name}")])
@task_name("processor")
def process_data(db, name):
    # read_object 内部自动处理 db_id
    raw = db.read_object(f"input/{name}")
    result = algorithm(raw)
    # write_object 自动跟踪写入
    db.write_object(f"output/{name}.result", result)

db = Database("/data/project")
process_data(db, "file1")  # 一行提交任务
```

**关键 API 简化对比**：

| 操作 | 原方式（手动拼接） | 新方式（自动处理） |
|------|-------------------|-------------------|
| 唯一标识符 | `f"{db.get_db_id()}:key"` | `db.get_obj_name("key")` |
| 依赖声明 | `[f"{db_id}:input/{name}"]` | `[db.get_obj_name(f"input/{name}")]` |
| 写入跟踪 | 手动设置 `outputs` | `write_object` 自动跟踪 |
| 数据读取 | 需要知道 db_id | `read_object` 内部处理 |

---

## 八、约束与偏好

- 后续流程使用中文进行回复
- 永远不要直接使用 `bazel build` 或 `bazel test` 命令，必须使用 `./fly.sh` 脚本
- 项目使用 gcc12 编译器
- C++20 标准
- TDD approach: write failing test, implement, pass, commit