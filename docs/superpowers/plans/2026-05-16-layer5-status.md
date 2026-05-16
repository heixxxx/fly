# Layer 5 Python API + 写入跟踪 — 实施状态

**日期**: 2026-05-16
**状态**: Phase 1 待开始 (0/19 完成)

---

## 设计文档

完整设计见 `2026-05-16-layer5-python-api-design.md`

---

## 实施任务清单

### Phase 1: 写入跟踪核心 (7 tasks)

| Task | 内容 | 状态 | 优先级 |
|------|------|------|--------|
| 1.1 | WorkerAgent添加begin_task/end_task/record_write接口 | pending | high |
| 1.2 | WorkerAgentContext全局上下文管理 | pending | high |
| 1.3 | Database.write_object调用Agent.record_write | pending | high |
| 1.4 | Database.db_id生成和get_obj_name()方法 | pending | high |
| 1.5 | TaskCompleteMessage使用完整标识符格式 | pending | medium |
| 1.6 | Python导出get_obj_name和自动写入跟踪 | pending | high |
| 1.7 | 测试：多db同名对象写入和get_obj_name | pending | high |

### Phase 2: Python高层API (6 tasks)

| Task | 内容 | 状态 | 优先级 |
|------|------|------|--------|
| 2.1 | fly/__init__.py顶层包 | pending | medium |
| 2.2 | fly/task.py @as_task装饰器 | pending | high |
| 2.3 | fly/task.py @task_name装饰器 | pending | medium |
| 2.4 | fly/master.py Master类包装和launch_local_workers() | pending | high |
| 2.5 | fly/config.py Config包装 | pending | medium |
| 2.6 | 测试：完整用户代码流程 | pending | high |

### Phase 3: Worker自动执行 (6 tasks)

| Task | 内容 | 状态 | 优先级 |
|------|------|------|--------|
| 3.1 | Worker Agent自动import module | pending | high |
| 3.2 | Worker Agent pickle args反序列化 | pending | high |
| 3.3 | Worker Agent执行原始函数_fly_original_func | pending | high |
| 3.4 | fly-worker启动脚本 | pending | medium |
| 3.5 | fly主命令行入口 | pending | medium |
| 3.6 | 测试：端到端用户脚本执行 | pending | high |

---

## 关键设计要点

### 写入跟踪机制

```
任务执行 → db.write_object(name)
         → Agent.record_write(db_id, name)
         → Agent.end_task()
         → TaskCompleteMessage.written_objects=["db_id:name"]
         → Master.mark_data_ready("db_id:name")
```

### API简化

```python
# 用户代码
db.get_obj_name("output/result")  # 返回 "db_id:output/result"

@as_task(inputs=lambda db, name: [db.get_obj_name(f"input/{name}")])
def process(db, name):
    db.write_object(f"output/{name}", data)  # 自动跟踪
```

### db_id生成策略

自动生成基于路径哈希：
```python
db = Database("/data/project_a")
# db_id = hash("/data/project_a") → 自动生成
```

---

## 文件结构规划

```
src/fly/
├── __init__.py       # 顶层包导出
├── master.py         # Master类包装
├── config.py         # Config包装
├── task.py           # @as_task, @task_name装饰器
└── worker/
    └── executor.py   # Worker执行器

src/agent/cpp/
├── worker_agent.h/cpp  # 添加begin_task/end_task/record_write
└── worker_context.h    # WorkerAgentContext全局上下文

src/storage/cpp/
├── database.h/cpp      # 添加db_id, get_obj_name(), write跟踪
```

---

## 当前状态

- **Git**: 工作区干净
- **提交**: 72 commits (含设计文档)
- **测试**: 161 tests pass
- **设计文档**: 已更新写入跟踪修正方案

---

## 下次继续

从 Phase 1.1 开始：
- WorkerAgent添加begin_task/end_task/record_write接口
- TDD流程：先写测试再实现

---

## 约束

- 使用 `./fly.sh` 而非裸 bazel
- gcc12 编译器
- C++20 标准
- TDD approach