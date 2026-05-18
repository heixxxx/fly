# Layer 5 Python API + 数据传输 — 实施状态

**日期**: 2026-05-17 (v2)
**状态**: ✅ 全部完成 (Phase 1-3 + 数据传输 + Python API 重构 + DataService 迁移)

---

## 设计文档

- 完整设计见 `2026-05-16-layer5-python-api-design.md`
- 数据传输设计见 `specs/2026-05-12-distributed-task-framework-design.md` Section 9.5, 10.2, 19.4, 20.4

---

## 实施任务清单

### Phase 1: 写入跟踪核心 (7/7 完成)

| Task | 内容 | 状态 |
|------|------|------|
| 1.1 | WorkerAgent添加begin_task/end_task/record_write接口 | ✅ |
| 1.2 | WorkerAgentContext全局上下文管理 | ✅ |
| 1.3 | Database.write_object调用Agent.record_write | ✅ |
| 1.4 | Database.db_id生成和get_obj_name()方法 | ✅ |
| 1.5 | TaskCompleteMessage使用完整标识符格式 | ✅ |
| 1.6 | Python导出get_obj_name和自动写入跟踪 | ✅ |
| 1.7 | 测试：多db同名对象写入和get_obj_name | ✅ |

### Phase 2: Python高层API (6/6 完成)

| Task | 内容 | 状态 |
|------|------|------|
| 2.1 | fly/__init__.py顶层包 | ✅ |
| 2.2 | fly/task.py @as_task装饰器 | ✅ |
| 2.3 | fly/database.py _Database类 + fly.open_db() 工厂函数 | ✅ |
| 2.4 | fly/agent.py Master/Worker类 | ✅ |
| 2.5 | fly/runtime.py 运行时配置 | ✅ |
| 2.6 | fly/executor.py Worker执行器 | ✅ |

### Phase 3: Worker自动执行 (8/8 完成)

| Task | 内容 | 状态 |
|------|------|------|
| 3.1 | Worker Agent import module + 执行原始函数 | ✅ |
| 3.2 | pickle args反序列化 | ✅ |
| 3.3 | fly binary C++入口 (main.cpp) | ✅ |
| 3.4 | Worker poll_task()循环 | ✅ |
| 3.5 | 递归任务提交 | ✅ |
| 3.6 | E2E测试套件 | ✅ |
| 3.7 | DB freeze通过消息传递 | ✅ |
| 3.8 | Database C++侧管理 (MasterAgent) | ✅ |

### 数据传输协议 → DataService 迁移 (9/9 完成)

| Task | 内容 | 状态 |
|------|------|------|
| D1 | DataService 单例实现 (local_idx + remote_idx + worker_registry) | ✅ |
| D2 | DataWriter::get_last_entry() 暴露索引条目 | ✅ |
| D3 | DataReader::read_from_entries() 直接按 IndexEntry 读取 + ObjectHeader py_name 提取 | ✅ |
| D4 | Database 写时通知 DataService, 读时走 DataService (模板 + 非模板统一) | ✅ |
| D5 | MetadataManager 清理 (移除数据位置方法, 仅保留 task 元数据) | ✅ |
| D6 | MasterAgent 迁移到 DataService (DataQuery, on_data_ready, on_task_complete) | ✅ |
| D7 | WorkerAgent.request_data_from_worker + 远程读取后更新 remote_idx | ✅ |
| D8 | Python database.py 三层读取流程 (local→remote_idx缓存→全程远程, 3次重试) | ✅ |
| D9 | DB Path 查询: 独立消息类型 (19/20), WorkerAgent.request_db_path() + 自动创建 Database | ✅ |

### Python API 重构 (5/5 完成)

| Task | 内容 | 状态 |
|------|------|------|
| R1 | Database→_Database 内部化, 新增 fly.open_db() 工厂函数 | ✅ |
| R2 | _Database 吸收 FlyDatabase 的 C++ 类型感知序列化 (is_cpp 双路径) | ✅ |
| R3 | _Database 补齐 6 个代理方法 (write/read_object_raw, load_meta, get_base/data_path, reset) | ✅ |
| R4 | 删除遗留 FlyDatabase (src/storage/py/database.py) | ✅ |
| R5 | EXStgIndexEntry/DbMeta/WorkerInfo 导出带参构造函数 + 修复 __setstate__ placement new bug | ✅ |

---

## 关键实现细节

### Database 创建方式

```python
import fly

db = fly.open_db("/path/to/db")
db.write_object("key", value)
result = db.read_object("key")
```

- `fly.open_db(path)` 是唯一公开的 Database 创建接口
- `_Database` 为内部类，Worker executor 内部可直接构造
- `write_object` / `read_object` 自动检测 C++ 导出类型（`is_cpp` 属性），使用 `__getstate__`/`__setstate__` 透传序列化

### C++ 导出类型构造函数

```python
from _fly_storage import EXStgIndexEntry, EXStgDbMeta, EXStgWorkerInfo

entry = EXStgIndexEntry("name", "file.dat", 100, 512, False, 0, 0)
meta = EXStgDbMeta("/db/id", "/base", 1000, 2000)
worker = EXStgWorkerInfo(1, "host", "role", "/data", "w1.idx", 100, "")
```

- 属性为 readonly，通过构造函数一次性设置
- `__setstate__` 使用 placement new 确保未初始化对象安全反序列化

### 线程模型

```
Worker Node:
├── Main Thread: poll_task → executor.execute (Python)
├── Reactor Thread: 消息处理 + Data Server (listen) + IOThreadPool.process_completions()
├── IOThreadPool: DataService 异步文件 I/O (可配置线程数, 默认1)
└── Heartbeat Thread: 心跳发送 (CV-based, 非sleep)
```

### 数据传输流程 (三层降级读取)

```
Python: db.read_object("key")

Layer 1: 本地读取
  └── DataService.try_read_local(key)
        ├── 找到 → DataReader.read_from_entries() → 返回
        └── 未找到 → 进入 Layer 2

Layer 2: 远程索引缓存
  └── DataService.lookup_remote_idx(key)
        ├── 有缓存 → WorkerAgent.request_data_from_worker(host, port, key)
        │     └── 直连目标 Worker, 跳过 Master
        └── 失败/无缓存 → 进入 Layer 3

Layer 3: 全程远程 (最多 3 次重试)
  └── WorkerAgent.request_remote_data(key)
        ├── DataQuery → Master → DataLocation
        ├── connect → target Worker → DataRequest → DataResponse
        └── 成功 → DataService.update_remote_idx() (缓存)
```

### 关键设计决策

- **DataService modeless**: Master 和 Worker 共享同一个 DataService 类，仅更新触发源不同
- **remote_idx as cache**: Worker 远程读取成功后自动缓存，后续同对象跳过 Master 查询
- **MetadataManager task-only**: 数据位置跟踪已迁移至 DataService，MetadataManager 仅管理 TaskMetadata
- **Database::read_object_typed delegates to DataService**: 本地未找到时 throw，Python 处理远程重试
- **Worker Data Server**: 每个Worker启动时listen端口,注册时上报给Master
- **Database shared_ptr**: 所有Database引用使用shared_ptr,不使用裸指针
- **fly.open_db()**: 唯一公开的 Database 创建入口, _Database 为内部类
- **C++ type-aware serialization**: write/read_object 自动检测 is_cpp 属性, C++ 导出类型走 __getstate__/__setstate__
- **DB freeze via message**: Worker通过TaskCompleteMessage.frozen_dbs通知Master,Master调用C++ Database::freeze()
- **Heartbeat CV**: 心跳线程使用 condition_variable::wait_for() 替代 sleep_for(), stop() 无阻塞
- **IOThreadPool completion pattern**: 任务在线程池执行文件 I/O, 完成回调通过 process_completions() 在 Reactor 线程执行 → reactor_->send(), 避免跨线程 transport 访问
- **DataClient 独立连接**: Worker A 读取使用阻塞 TCP socket (DataClient), 独立于主 Reactor, 避免多线程读冲突

---

## 测试状态

- **C++ 测试**: 33 Bazel targets pass (含 data_service_test 12 cases + data_transfer_test 10 cases)
- **QA 测试**: 2 targets pass
- **E2E 测试**: 5/5 pass (via `./bazel-bin/src/main/cpp/fly src/e2e_user_script.py`)
  1. test_worker_db_write: Worker写, Master读
  2. test_dependency_and_freeze: 依赖+freeze
  3. test_read_frozen_db: 读frozen DB
  4. test_write_frozen_db_fails: 写frozen DB被拒绝
  5. test_recursive_submit: 递归提交子任务

---

## 下一步

- 写入注册协议 (Worker→Master ACK, freeze 检测, 失败回滚)
- 跨 Worker 数据读取 E2E 测试 (基于新 DataService 三层流程)
- Database freeze 后处理 (merged.idx, _META, 恢复加载)
- Locality 优化调度
- 数据副本与容错机制
