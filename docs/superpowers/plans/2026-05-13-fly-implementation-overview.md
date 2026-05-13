# Fly 分布式任务框架 — 实现总览

> **Goal:** 实现完整的分布式任务执行框架，从底层基础设施到上层应用功能分5层递进开发

> **Architecture:** C++核心（存储、通信、调度）+ Python流程控制（任务定义、主循环）+ pybind11桥接，Bazel构建，每个模块独立so

> **Tech Stack:** C++17, Python 3.10+, pybind11, Bazel, gtest, pytest, cereal

---

## 分层实现策略

每层完成后必须通过**单元测试**才能进入下一层。项目级集成测试放在`qa/`目录。

```
Layer 0: 项目基础设施 + 构建系统
    ↓ 测试通过
Layer 1: 核心存储层 (Database, DataWriter, DataReader, LocalIndex, Serializer)
    ↓ 测试通过
Layer 2: 通信层 + 序列化协议 (TransportLayer, MessageProtocol, Messages)
    ↓ 测试通过
Layer 3: 任务系统 + 依赖图 (TaskRegistry, DependencyGraph, @as_task)
    ↓ 测试通过
Layer 4: Master/Worker Agent + Reactor + Config + 启动流程
    ↓ 测试通过
Layer 5: 高级功能 (Database Freeze, 备份, 多DB, 交互模式)
    ↓ 测试通过
Layer 6: 集成测试 + 端到端测试 (qa/)
```

---

## 项目目录结构

```
fly/
├── WORKSPACE                    # Bazel workspace
├── .bazelrc                     # Bazel配置
├── src/
│   ├── core/                    # 核心基础模块
│   │   ├── cpp/
│   │   │   ├── config.cpp
│   │   │   ├── config.h
│   │   │   ├── database.cpp
│   │   │   ├── database.h
│   │   │   ├── data_writer.cpp
│   │   │   ├── data_writer.h
│   │   │   ├── data_reader.cpp
│   │   │   ├── data_reader.h
│   │   │   ├── local_index.cpp
│   │   │   ├── local_index.h
│   │   │   ├── serializer.cpp
│   │   │   ├── serializer.h
│   │   │   ├── storage_manager.cpp
│   │   │   ├── storage_manager.h
│   │   │   ├── transport.cpp
│   │   │   ├── transport.h
│   │   │   ├── reactor.cpp
│   │   │   ├── reactor.h
│   │   │   ├── message_protocol.cpp
│   │   │   ├── message_protocol.h
│   │   │   ├── message_types.h
│   │   │   ├── db_meta.cpp
│   │   │   └── db_meta.h
│   │   ├── export/
│   │   │   └── core_export.cpp
│   │   ├── py/
│   │   │   ├── __init__.py
│   │   │   ├── connection.py
│   │   │   └── protocol.py
│   │   └── tests/
│   │       ├── config_test.cpp
│   │       ├── database_test.cpp
│   │       ├── data_writer_test.cpp
│   │       ├── local_index_test.cpp
│   │       ├── serializer_test.cpp
│   │       ├── transport_test.cpp
│   │       └── test_core.py
│   │
│   ├── serialization/
│   │   ├── cpp/
│   │   │   ├── serialization_macros.h
│   │   │   └── BUILD
│   │   └── tests/
│   │       └── serialization_test.cpp
│   │
│   ├── export/
│   │   └── cpp/
│   │       ├── export_macros.h
│   │       └── BUILD
│   │
│   ├── master/
│   │   ├── cpp/
│   │   ├── export/
│   │   ├── py/
│   │   └── tests/
│   │
│   ├── worker/
│   │   ├── cpp/
│   │   ├── export/
│   │   ├── py/
│   │   └── tests/
│   │
│   ├── task/
│   │   ├── py/
│   │   └── tests/
│   │
│   └── main.cpp
├── qa/                          # 项目级集成测试
│   ├── test_master_worker.py
│   ├── test_task_dependency.py
│   ├── test_database_freeze.py
│   └── test_e2e.py
├── docs/
│   └── superpowers/
│       ├── specs/
│       └── plans/
└── BUILD                        # 顶层BUILD文件
```

---

## 各层详细计划文件

| 层 | 计划文件 | 核心产出 |
|----|---------|---------|
| Layer 0 | `2026-05-13-fly-layer0-infrastructure.md` | WORKSPACE, BUILD, config, serialization_macros, export_macros |
| Layer 1 | `2026-05-13-fly-layer1-storage.md` | Database, DataWriter, DataReader, LocalIndex, StorageManager, Serializer |
| Layer 2 | `2026-05-13-fly-layer2-communication.md` | TransportLayer(TCP), MessageProtocol, MessageTypes, Reactor base |
| Layer 3 | `2026-05-13-fly-layer3-task-system.md` | @as_task, TaskRegistry, DependencyGraph, Config Python接口 |
| Layer 4 | `2026-05-13-fly-layer4-agents.md` | MasterAgent, WorkerAgent, MasterReactor, WorkerReactor, 启动流程 |
| Layer 5 | `2026-05-13-fly-layer5-advanced.md` | Database Freeze, 备份管理, 多DB, 交互模式, _META持久化 |
| Layer 6 | `2026-05-13-fly-layer6-integration.md` | qa/ 集成测试, 端到端测试 |

每层计划独立可执行，产出经过单元测试验证的可工作软件。

---

## 实施原则

1. **TDD**: 每个模块先写测试，再写实现
2. **每个模块独立so**: Bazel构建，模块间依赖通过BUILD声明
3. **频繁提交**: 每个测试通过后立即commit
4. **C++测试**: gtest，放在`src/<module>/tests/`
5. **Python测试**: pytest，放在`src/<module>/tests/`或`qa/`
6. **pybind11导出**: 每个模块的`export/`目录负责C++到Python的桥接
7. **宏封装**: 序列化用`FLY_SERIALIZE_*`，导出用`FLY_EXPORT_*`

---

## 下一步

选择从 Layer 0 开始实现，或指定其他层。