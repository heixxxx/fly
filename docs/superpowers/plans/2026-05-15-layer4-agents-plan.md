# Layer 4: Master/Worker Agents 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 实现分布式任务框架的 Master/Worker Agent 层，Master 负责任务调度和 Worker 管理，Worker 负责任务执行和数据管理。

**Architecture:**
- **MasterAgent**: 整合 TaskScheduler + WorkerManager + HeartbeatMonitor + Reactor(TCP Server)
  - 接收 Worker 注册/心跳/任务完成消息
  - 调度就绪任务到空闲 Worker
  - 监控 Worker 心跳超时
  - 提供 Python 接口：launch_local_workers, launch_ssh_workers
- **WorkerAgent**: 整合 Reactor(TCP Client) + TaskExecutor + StorageManager
  - 连接 Master 并注册
  - 接收并执行任务
  - 发送心跳和任务状态
  - 管理本地数据库

**Tech Stack:**
- C++20 (std::thread, std::atomic, std::function)
- nanobind (Python 绑定)
- Layer 2 Network (Reactor, TransportLayer, MessageProtocol)
- Layer 3 Task (DependencyGraph, WorkerManager, TaskScheduler, MetadataManager, HeartbeatMonitor)
- Layer 1 Storage (StorageManager, Database)
- gtest (单元测试)
- pytest (Python 集成测试)
- Bazel（构建系统）

---

## 文件结构

```
src/agent/
├── cpp/
│   ├── master_agent.h              # Master Agent
│   ├── master_agent.cpp            # Master 实现
│   ├── worker_agent.h              # Worker Agent
│   ├── worker_agent.cpp            # Worker 实现
│   ├── task_executor.h             # 任务执行器
│   ├── task_executor.cpp           # 执行器实现
│   └── BUILD                       # cc_library targets
├── export/
│   ├── agent_export.cpp            # nanobind 导出
│   └── BUILD                       # cc_binary: _fly_agent.so
├── py/
│   ├── __init__.py                 # from _fly_agent import *
│   └── BUILD                       # py_library
└── tests/
    ├── master_agent_test.cpp       # Master 测试
    ├── worker_agent_test.cpp       # Worker 测试
    ├── agent_integration_test.cpp  # Master+Worker 集成测试
    ├── test_agent_integration.py   # Python 集成测试
    └── BUILD                       # cc_test / py_test
```

---

## Task 1: TaskExecutor 任务执行器

**Files:**
- Create: `src/agent/cpp/task_executor.h`
- Create: `src/agent/cpp/task_executor.cpp`
- Create: `src/agent/tests/task_executor_test.cpp`
- Create: `src/agent/cpp/BUILD`

**职责:** 在 Worker 端执行 Python 任务模块中的函数

- [ ] **Step 1: Write failing test**
- [ ] **Step 2: Run test to verify it fails**
- [ ] **Step 3: Implement TaskExecutor header + cpp**
- [ ] **Step 4: Create BUILD file**
- [ ] **Step 5: Run test to verify it passes**
- [ ] **Step 6: Commit**

---

## Task 2: MasterAgent Master 节点

**Files:**
- Create: `src/agent/cpp/master_agent.h`
- Create: `src/agent/cpp/master_agent.cpp`
- Create: `src/agent/tests/master_agent_test.cpp`

**职责:**
- 启动 TCP Server (Reactor)
- 处理 Worker 注册/心跳/任务完成/数据就绪消息
- 整合 TaskScheduler + WorkerManager + HeartbeatMonitor
- 提供 launch_local_workers / launch_ssh_workers 接口

- [ ] **Step 1: Write failing test**
- [ ] **Step 2: Run test to verify it fails**
- [ ] **Step 3: Implement MasterAgent header + cpp**
- [ ] **Step 4: Update BUILD file**
- [ ] **Step 5: Run test to verify it passes**
- [ ] **Step 6: Commit**

---

## Task 3: WorkerAgent Worker 节点

**Files:**
- Create: `src/agent/cpp/worker_agent.h`
- Create: `src/agent/cpp/worker_agent.cpp`
- Create: `src/agent/tests/worker_agent_test.cpp`

**职责:**
- 连接 Master (TCP Client via Reactor)
- 发送注册消息
- 定期发送心跳
- 接收 TaskAssignMessage 并交给 TaskExecutor
- 任务完成后发送 TaskCompleteMessage

- [ ] **Step 1: Write failing test**
- [ ] **Step 2: Run test to verify it fails**
- [ ] **Step 3: Implement WorkerAgent header + cpp**
- [ ] **Step 4: Update BUILD file**
- [ ] **Step 5: Run test to verify it passes**
- [ ] **Step 6: Commit**

---

## Task 4: Master+Worker 集成测试 (C++)

**Files:**
- Create: `src/agent/tests/agent_integration_test.cpp`

**测试场景:**
- Master 启动 → Worker 连接注册 → 提交任务 → Worker 执行 → 任务完成
- 多 Worker 并发任务调度
- Worker 心跳超时检测
- 任务依赖链端到端

- [ ] **Step 1: Write integration test**
- [ ] **Step 2: Run test to verify it passes**
- [ ] **Step 3: Commit**

---

## Task 5: Python 导出和集成测试

**Files:**
- Create: `src/agent/export/agent_export.cpp`
- Create: `src/agent/export/BUILD`
- Create: `src/agent/py/__init__.py`
- Create: `src/agent/py/BUILD`
- Create: `src/agent/tests/test_agent_integration.py`

- [ ] **Step 1: Write failing test**
- [ ] **Step 2: Run test to verify it fails**
- [ ] **Step 3: Implement agent_export.cpp**
- [ ] **Step 4: Create BUILD files**
- [ ] **Step 5: Run test to verify it passes**
- [ ] **Step 6: Commit**

---

## 实施顺序总结

```
Task 1: TaskExecutor  → Task 2: MasterAgent  → Task 3: WorkerAgent
                                                    ↓
Task 4: C++ 集成测试  → Task 5: Python 导出和集成测试
```

---

## 预计完成时间

**预计时间**: 2-3 天（每个 Task 0.5 天，包括测试和构建调试）

**开始条件**: Layer 3 Task System 已完成并通过所有测试
