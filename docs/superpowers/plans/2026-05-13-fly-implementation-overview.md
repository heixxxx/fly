# Fly 分布式任务框架 — 实现总览

> **Goal:** 实现完整的分布式任务执行框架，从底层基础设施到上层应用功能分5层递进开发

> **Architecture:** C++核心（存储、通信、调度）+ Python流程控制（任务定义、主循环）+ nanobind桥接，Bazel构建，每个模块独立so

> **Tech Stack:** C++20, Python 3.10+, nanobind, Bazel, gtest, pytest, cereal

---

## 当前实现状态

| 层 | 状态 | 测试数 | 提交数 | 完成日期 |
|----|------|--------|--------|----------|
| Layer 0 | ✅ 完成 | 5 | 8 | 2026-05-13 |
| Layer 1 | ✅ 完成 | 45 | 12 | 2026-05-14 |
| Layer 2 | ✅ 完成 | 35 | 15 | 2026-05-15 |
| Layer 3 | ✅ 完成 | 28 | 18 | 2026-05-15 |
| Layer 4 | ✅ 完成 | 48 | 35 | 2026-05-16 |
| Layer 5+6 | 待实施 | - | - | - |

**总提交**: 66 commits
**总测试**: ~161 tests

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
├── fly.sh                       # 构建脚本 (必须使用此脚本而非裸 bazel)
├── src/
│   ├── common/                  # 公共类型定义
│   │   └── cpp/common_types.h   # CMString, CMVector, CMMap 等类型别名
│   │
│   ├── core/                    # 核心基础模块
│   │   └── cpp/config.h/cpp     # 配置管理
│   │
│   ├── serialization/           # 序列化模块
│   │   └ cpp/
│   │   │   ├── serialization_macros.h
│   │   │   └ BUILD
│   │   └ tests/
│   │       └ serialization_test.cpp
│   │
│   ├── export/                  # 导出宏
│   │   └ cpp/
│   │   │   ├── export_macros.h  # nanobind 导出宏封装
│   │   │   └ BUILD
│   │
│   ├── compression/             # 压缩层
│   │   └ cpp/
│   │   │   ├── compressor.h/cpp
│   │   │    BUILD
│   │   └ tests/
│   │
│   ├── storage/                 # 存储层 (Layer 1)
│   │   └ cpp/
│   │   │   ├── database.h/cpp
│   │   │   ├── data_writer.h/cpp
│   │   │   ├── data_reader.h/cpp
│   │   │   ├── object.h/cpp
│   │   │   ├── index_entry.h/cpp
│   │   │   ├── db_meta.h/cpp
│   │   │   ├── storage_manager.h/cpp
│   │   │   └ BUILD
│   │   └ export/
│   │   │   ├── storage_export.cpp
│   │   │   └ BUILD (_fly_storage.so)
│   │   └ tests/
│   │
│   ├── network/                 # 网络层 (Layer 2)
│   │   └ cpp/
│   │   │   ├── reactor.h/cpp
│   │   │   ├── transport.h/cpp
│   │   │   ├── tcp_transport.cpp
│   │   │   ├── message_protocol.h/cpp
│   │   │   ├── message_types.h
│   │   │   └ BUILD
│   │   └ export/
│   │   │   ├── network_export.cpp
│   │   │   └ BUILD (_fly_network.so)
│   │   └ tests/
│   │
│   ├── task/                    # 任务系统层 (Layer 3)
│   │   └ cpp/
│   │   │   ├── dependency_graph.h/cpp
│   │   │   ├── worker_manager.h/cpp
│   │   │   ├── task_scheduler.h/cpp
│   │   │   ├── metadata_manager.h/cpp
│   │   │   ├── heartbeat_monitor.h/cpp
│   │   │   └ BUILD
│   │   └ tests/
│   │
│   ├── agent/                   # Agent层 (Layer 4)
│   │   └ cpp/
│   │   │   ├── task_executor.h/cpp
│   │   │   ├── master_agent.h/cpp
│   │   │   ├── worker_agent.h/cpp
│   │   │   └ BUILD
│   │   └ export/
│   │   │   ├── agent_export.cpp
│   │   │   └ BUILD (_fly_agent.so)
│   │   └ tests/
│   │     ├── test_agent_integration.py
│   │     └ test_sum_example.py  (端到端示例)
│   │
│   └ log/                       # 日志模块
│   │   └ cpp/
│   │   │   ├── logger.h/cpp
│   │   │   └ BUILD
│   │   └ export/
│   │   │   ├── log_export.cpp
│   │   │   └ BUILD (_fly_log.so)
│   │   └ tests/
│   │
│   └ main.cpp
├── qa/                          # 项目级集成测试
│   ├── test_master_worker.py
│   ├── test_task_dependency.py
│   ├── test_database_freeze.py
│   └ test_e2e.py
├── docs/
│   └ superpowers/
│   │   ├── specs/               # 设计文档
│   │   └ plans/                 # 实施计划
│   └     ├── 2026-05-13-fly-implementation-overview.md
│   └     ├── 2026-05-13-fly-layer0-infrastructure.md
│   └     ├── 2026-05-14-fly-layer1-storage-layer.md
│   └     ├── 2026-05-15-layer2-networking-plan.md
│   └     ├── 2026-05-15-layer3-task-system-plan.md
│   └     ├── 2026-05-15-layer4-agents-plan.md
│   └     ├── 2026-05-15-layer4-progress.md
│   └     ├── 2026-05-15-layer4-phase2-implementation.md
│   └     ├── 2026-05-15-layer4-phase3-implementation.md
│   └     └ 2026-05-15-layer4-phase2-network-design.md
│   └     └ 2026-05-12-distributed-task-framework-design.md
└── BUILD                        # 顶层BUILD文件 (compile_commands.json refresh)
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
6. **nanobind导出**: 每个模块的`export/`目录负责C++到Python的桥接
7. **宏封装**: 序列化用`FLY_SERIALIZE_*`，导出用`FLY_EXPORT_*`
8. **构建约束**:
   - 必须使用 `./fly.sh` 而非裸 `bazel build/test`
   - C++20 标准: `--copt=-std=c++20`
   - 编译器: gcc12 (非 clang)
   - fly.sh 会自动刷新 compile_commands.json 供 clangd 使用

---

## Layer 4 完成详情

**MasterAgent**:
- Reactor TCP Server 集成
- TaskScheduler/WorkerManager/MetadataManager/HeartbeatMonitor 集成
- submit_task() API 和任务状态查询
- Worker 注册/心跳/任务完成消息处理
- Python callable 作为执行函数支持

**WorkerAgent**:
- Reactor TCP Client 集成
- 独立心跳线程 (10s interval)
- TaskExecutor 外部注入
- 任务执行 → 结果发送流程
- 日志输出 (worker{id}.log)

**端到端示例**:
- test_sum_example.py: 分布式求和计算
- 3个 worker 分布式计算部分和
- 聚合任务汇总结果
- 数据库冻结验证

---

## 下一步

Layer 5+6 待实施:
- Layer 5: 高级功能 (Database Freeze完善, 备份管理, 多DB, 交互模式)
- Layer 6: 集成测试 + 端到端测试 (qa/)