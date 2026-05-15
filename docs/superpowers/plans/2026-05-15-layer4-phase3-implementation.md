# Layer 4 Phase 3 任务调度集成实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 Layer 3 Task System 集成到 MasterAgent/WorkerAgent，实现完整的任务提交→调度→执行→完成流程。

**Architecture:** MasterAgent 集成 TaskScheduler/WorkerManager/MetadataManager，实现 submit_task() API；WorkerAgent 完善任务执行状态跟踪。

**Tech Stack:** C++20, Layer 2 Network (Reactor), Layer 3 Task System, nanobind, gtest

---

## 文件结构

```
src/agent/
├── cpp/
│   ├── master_agent.h         # 修改: 添加 TaskScheduler 等组件
│   ├── master_agent.cpp       # 修改: 实现 submit_task/schedule_tasks
│   ├── worker_agent.h         # 修改: 完善 running_tasks_ 跟踪
│   ├── worker_agent.cpp       # 修改: 完善任务状态报告
│   └── BUILD                  # 修改: 添加 task 依赖
├── export/
│   ├── agent_export.cpp       # 修改: 添加 submit_task 等导出
│   └── BUILD                  # 保持不变
└── tests/
    ├── agent_network_test.cpp # 修改: 添加端到端测试
    ├── test_agent_integration.py # 修改: 添加 Python 测试
    └── BUILD                  # 保持不变
```

---

## Task 1: MasterAgent 集成 TaskScheduler 和 WorkerManager

**Files:**
- Modify: `src/agent/cpp/master_agent.h`
- Modify: `src/agent/cpp/master_agent.cpp`
- Modify: `src/agent/cpp/BUILD`

- [ ] **Step 1: 修改 master_agent.h 添加 TaskScheduler 和 WorkerManager**

```cpp
#pragma once

#include <network/cpp/reactor.h>
#include <network/cpp/transport.h>
#include <network/cpp/message_types.h>
#include <task/cpp/dependency_graph.h>
#include <task/cpp/worker_manager.h>
#include <task/cpp/task_scheduler.h>
#include <log/cpp/logger.h>
#include <common/cpp/common_types.h>
#include <cstdint>
#include <thread>
#include <atomic>
#include <map>
#include <memory>

namespace fly {

class MasterAgent {
public:
    MasterAgent(const CMString& host, uint16_t port);
    ~MasterAgent();
    
    void start();
    void stop();
    bool is_running() const;
    
    // Phase 2: 连接查询
    CMVector<uint64_t> get_connected_workers() const;
    size_t get_connection_count() const;
    
    // Phase 3: 任务提交
    void submit_task(uint64_t task_id, const CMString& name,
                    const CMString& module, const CMVector<CMString>& args,
                    const CMVector<CMString>& inputs = {},
                    const CMVector<CMString>& outputs = {});
    
    // Phase 3: 任务状态查询
    CMVector<uint64_t> get_pending_tasks() const;
    CMVector<uint64_t> get_running_tasks() const;
    CMVector<uint64_t> get_completed_tasks() const;
    
    // Phase 3: Worker 管理
    CMVector<uint64_t> get_idle_workers() const;
    
private:
    CMString host_;
    uint16_t port_;
    std::atomic<bool> running_{false};
    
    // Phase 2: 网络层
    std::unique_ptr<Reactor> reactor_;
    std::thread reactor_thread_;
    
    // Phase 2: 连接映射
    CMMap<uint64_t, uint64_t> conn_to_worker_;
    CMMap<uint64_t, uint64_t> worker_to_conn_;
    
    // Phase 3: 任务调度
    std::unique_ptr<DependencyGraph> graph_;
    std::unique_ptr<WorkerManager> worker_manager_;
    std::unique_ptr<TaskScheduler> scheduler_;
    
    // Phase 3: 内部调度
    void schedule_tasks();
    void assign_task_to_worker(uint64_t task_id, uint64_t worker_id);
    
    // Phase 2: 消息处理
    void on_worker_register(uint64_t conn_id, const RegisterMessage& msg);
    void on_heartbeat(uint64_t conn_id, const HeartbeatMessage& msg);
    void on_task_complete(uint64_t conn_id, const TaskCompleteMessage& msg);
    void on_task_failed(uint64_t conn_id, const TaskFailedMessage& msg);
    void on_disconnect(uint64_t conn_id);
    void on_error(uint64_t conn_id, int error_code);
};

}  // namespace fly
```

- [ ] **Step 2: 修改 master_agent.cpp 实现任务调度**

```cpp
#include <agent/cpp/master_agent.h>
#include <log/cpp/logger.h>
#include <thread>
#include <chrono>

namespace fly {

MasterAgent::MasterAgent(const CMString& host, uint16_t port)
    : host_(host), port_(port), running_(false),
      graph_(std::make_unique<DependencyGraph>()),
      worker_manager_(std::make_unique<WorkerManager>()) {}

void MasterAgent::start() {
    if (running_) return;
    
    auto* log = Logger::get_master();
    if (log) {
        log->info("MasterAgent", "start() called");
    }
    
    auto transport = create_transport("tcp");
    transport->listen(host_, port_);
    
    reactor_ = std::make_unique<Reactor>(std::move(transport));
    
    // Phase 2: 网络消息处理
    reactor_->register_handler<RegisterMessage>(
        [this](uint64_t conn_id, const RegisterMessage& msg) {
            on_worker_register(conn_id, msg);
        });
    
    reactor_->register_handler<HeartbeatMessage>(
        [this](uint64_t conn_id, const HeartbeatMessage& msg) {
            on_heartbeat(conn_id, msg);
        });
    
    // Phase 3: 任务完成/失败处理
    reactor_->register_handler<TaskCompleteMessage>(
        [this](uint64_t conn_id, const TaskCompleteMessage& msg) {
            on_task_complete(conn_id, msg);
        });
    
    reactor_->register_handler<TaskFailedMessage>(
        [this](uint64_t conn_id, const TaskFailedMessage& msg) {
            on_task_failed(conn_id, msg);
        });
    
    reactor_->on_disconnect([this](uint64_t conn_id) {
        on_disconnect(conn_id);
    });
    
    reactor_->on_error([this](uint64_t conn_id, int err) {
        on_error(conn_id, err);
    });
    
    // Phase 3: 创建 TaskScheduler (依赖 graph_ 和 worker_manager_)
    scheduler_ = std::make_unique<TaskScheduler>(graph_.get(), worker_manager_.get());
    
    reactor_thread_ = std::thread([this] { reactor_->run(); });
    running_ = true;
}

// Phase 3: 任务提交
void MasterAgent::submit_task(uint64_t task_id, const CMString& name,
                               const CMString& module, const CMVector<CMString>& args,
                               const CMVector<CMString>& inputs,
                               const CMVector<CMString>& outputs) {
    auto* log = Logger::get_master();
    if (log) {
        log->info("MasterAgent", "submit_task: id=" + std::to_string(task_id) + ", name=" + name);
    }
    
    graph_->add_task(task_id, inputs);
    
    // 尝试立即调度
    schedule_tasks();
}

void MasterAgent::schedule_tasks() {
    auto results = scheduler_->schedule_all_available();
    
    for (const auto& result : results) {
        if (result.scheduled) {
            assign_task_to_worker(result.task_id, result.worker_id);
        }
    }
}

void MasterAgent::assign_task_to_worker(uint64_t task_id, uint64_t worker_id) {
    auto* log = Logger::get_master();
    if (log) {
        log->info("MasterAgent", "assign_task: task=" + std::to_string(task_id) + " to worker=" + std::to_string(worker_id));
    }
    
    auto conn_it = worker_to_conn_.find(worker_id);
    if (conn_it == worker_to_conn_.end()) {
        if (log) {
            log->error("MasterAgent", "worker not found: " + std::to_string(worker_id));
        }
        return;
    }
    
    uint64_t conn_id = conn_it->second;
    
    TaskAssignMessage msg;
    msg.task_id = task_id;
    // Phase 3.3 会添加完整的 task 信息
    reactor_->send(conn_id, msg);
    
    worker_manager_->assign_task(worker_id, task_id);
}

// Phase 3: 任务完成处理
void MasterAgent::on_task_complete(uint64_t conn_id, const TaskCompleteMessage& msg) {
    auto* log = Logger::get_master();
    if (log) {
        log->info("MasterAgent", "task complete: task_id=" + std::to_string(msg.task_id));
    }
    
    uint64_t worker_id = msg.worker_id;
    
    worker_manager_->complete_task(worker_id);
    graph_->mark_task_complete(msg.task_id);
    
    // 尝试调度下一个任务
    schedule_tasks();
}

void MasterAgent::on_task_failed(uint64_t conn_id, const TaskFailedMessage& msg) {
    auto* log = Logger::get_master();
    if (log) {
        log->error("MasterAgent", "task failed: task_id=" + std::to_string(msg.task_id) + ", error=" + msg.error_message);
    }
    
    uint64_t worker_id = msg.worker_id;
    
    worker_manager_->complete_task(worker_id);
    
    // Phase 3.5 会处理重试逻辑
}

CMVector<uint64_t> MasterAgent::get_pending_tasks() const {
    return graph_->get_ready_tasks();
}

CMVector<uint64_t> MasterAgent::get_running_tasks() const {
    // Phase 3.2 会集成 MetadataManager
    return {};
}

CMVector<uint64_t> MasterAgent::get_completed_tasks() const {
    return graph_->get_completed_tasks();
}

CMVector<uint64_t> MasterAgent::get_idle_workers() const {
    return worker_manager_->get_idle_workers();
}

}  // namespace fly
```

- [ ] **Step 3: 修改 BUILD 添加 task 依赖**

```python
# src/agent/cpp/BUILD 修改 fly_agent_master
cc_library(
    name = "fly_agent_master",
    hdrs = ["master_agent.h"],
    srcs = ["master_agent.cpp"],
    deps = [
        ":fly_agent_task_executor",
        "//src/common/cpp:fly_common_types",
        "//src/network/cpp:fly_network_reactor",
        "//src/network/cpp:fly_network_transport",
        "//src/task/cpp:fly_task_cpp",  # Phase 3 新增
        "//src/log/cpp:fly_log",  # Phase 3 新增
    ],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
)
```

- [ ] **Step 4: 运行测试验证不破坏 Phase 2**

Run: `./fly.sh test //src/agent/tests:master_agent_test --test_output=all`

Expected: 3 tests PASS

- [ ] **Step 5: Commit**

```bash
git add src/agent/cpp/master_agent.h src/agent/cpp/master_agent.cpp src/agent/cpp/BUILD
git commit -m "feat(agent): MasterAgent Phase 3 TaskScheduler/WorkerManager integration

- Integrate DependencyGraph, WorkerManager, TaskScheduler
- Add submit_task() API for task submission
- Add schedule_tasks() for automatic task assignment
- Handle TaskCompleteMessage and TaskFailedMessage
- Add get_pending_tasks(), get_completed_tasks(), get_idle_workers()

Phase 2 tests still pass"
```

---

## Task 2: MasterAgent 集成 MetadataManager 和 HeartbeatMonitor

**Files:**
- Modify: `src/agent/cpp/master_agent.h`
- Modify: `src/agent/cpp/master_agent.cpp`

- [ ] **Step 1: 修改 master_agent.h 添加 MetadataManager 和 HeartbeatMonitor**

```cpp
#include <task/cpp/metadata_manager.h>
#include <task/cpp/heartbeat_monitor.h>

class MasterAgent {
    // Phase 3: 任务元数据
    std::unique_ptr<MetadataManager> metadata_;
    std::unique_ptr<HeartbeatMonitor> heartbeat_monitor_;
    std::thread heartbeat_check_thread_;
    std::atomic<bool> heartbeat_check_running_{false};
    
    // Phase 3: 心跳检测循环
    void heartbeat_check_loop();
};
```

- [ ] **Step 2: 修改 master_agent.cpp 实现心跳监控和元数据管理**

```cpp
void MasterAgent::start() {
    // ... existing code ...
    
    // Phase 3: 元数据管理
    metadata_ = std::make_unique<MetadataManager>();
    
    // Phase 3: 心跳监控
    heartbeat_monitor_ = std::make_unique<HeartbeatMonitor>(worker_manager_.get(), 30);
    heartbeat_check_running_ = true;
    heartbeat_check_thread_ = std::thread([this] { heartbeat_check_loop(); });
}

void MasterAgent::heartbeat_check_loop() {
    while (heartbeat_check_running_) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        
        if (running_) {
            auto now = std::chrono::system_clock::now().time_since_epoch();
            auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(now).count();
            
            heartbeat_monitor_->check_all_workers(timestamp);
            
            auto dead = heartbeat_monitor_->get_dead_workers();
            for (uint64_t worker_id : dead) {
                auto* log = Logger::get_master();
                if (log) {
                    log->warn("MasterAgent", "worker timeout: " + std::to_string(worker_id));
                }
                
                auto conn_it = worker_to_conn_.find(worker_id);
                if (conn_it != worker_to_conn_.end()) {
                    reactor_->send(conn_it->second, ShutdownMessage());
                }
            }
        }
    }
}

void MasterAgent::submit_task(uint64_t task_id, const CMString& name,
                               const CMString& module, const CMVector<CMString>& args,
                               const CMVector<CMString>& inputs,
                               const CMVector<CMString>& outputs) {
    metadata_->create_task(task_id, name, inputs, outputs, "{}");
    graph_->add_task(task_id, inputs);
    schedule_tasks();
}

void MasterAgent::on_task_complete(uint64_t conn_id, const TaskCompleteMessage& msg) {
    metadata_->update_task_status(msg.task_id, TaskStatus::COMPLETED);
    // ... existing code ...
}

CMVector<uint64_t> MasterAgent::get_running_tasks() const {
    auto tasks = metadata_->get_tasks_by_status(TaskStatus::RUNNING);
    CMVector<uint64_t> ids;
    for (const auto& t : tasks) {
        ids.push_back(t.task_id);
    }
    return ids;
}
```

- [ ] **Step 3: 运行测试验证**

Run: `./fly.sh test //src/agent/tests:master_agent_test --test_output=all`

Expected: 3 tests PASS

- [ ] **Step 4: Commit**

```bash
git add src/agent/cpp/master_agent.h src/agent/cpp/master_agent.cpp
git commit -m "feat(agent): MasterAgent Phase 3 MetadataManager/HeartbeatMonitor integration

- Integrate MetadataManager for task lifecycle tracking
- Integrate HeartbeatMonitor with 30s timeout
- Add heartbeat_check_thread for periodic worker timeout detection
- Update submit_task() to create metadata
- Add get_running_tasks() implementation

Heartbeat timeout sends ShutdownMessage to dead workers"
```

---

## Task 3: MasterAgent submit_task() 完整实现和任务状态查询

**Files:**
- Modify: `src/agent/cpp/master_agent.cpp`

- [ ] **Step 1: 完善 assign_task_to_worker() 发送完整任务信息**

```cpp
void MasterAgent::assign_task_to_worker(uint64_t task_id, uint64_t worker_id) {
    auto* meta = metadata_->get_task(task_id);
    if (!meta) {
        auto* log = Logger::get_master();
        if (log) {
            log->error("MasterAgent", "task metadata not found: " + std::to_string(task_id));
        }
        return;
    }
    
    auto conn_it = worker_to_conn_.find(worker_id);
    if (conn_it == worker_to_conn_.end()) return;
    
    uint64_t conn_id = conn_it->second;
    
    TaskAssignMessage msg;
    msg.task_id = task_id;
    msg.task_name = meta->name;
    msg.task_module = "";  // Phase 3.4 会从 meta 获取
    msg.args = {};         // Phase 3.4 会从 meta 获取
    
    reactor_->send(conn_id, msg);
    
    metadata_->update_task_status(task_id, TaskStatus::RUNNING);
    metadata_->set_assigned_worker(task_id, worker_id);
    worker_manager_->assign_task(worker_id, task_id);
    
    auto* log = Logger::get_master();
    if (log) {
        log->info("MasterAgent", "assigned task " + std::to_string(task_id) + " to worker " + std::to_string(worker_id));
    }
}
```

- [ ] **Step 2: 添加测试验证 submit_task 流程**

```cpp
// 在 agent_network_test.cpp 添加
TEST_F(AgentNetworkTest, SubmitTask) {
    MasterAgent master("127.0.0.1", 19100);
    master.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    WorkerAgent worker(1, "127.0.0.1", 19100);
    worker.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    EXPECT_EQ(master.get_connection_count(), 1);
    
    // 提交任务
    master.submit_task(100, "test_task", "test_module", {}, {}, {});
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    auto pending = master.get_pending_tasks();
    auto completed = master.get_completed_tasks();
    
    // 任务应该被调度或完成
    EXPECT_TRUE(pending.size() > 0 || completed.size() > 0);
    
    master.stop();
    worker.stop();
}
```

- [ ] **Step 3: 运行测试**

Run: `./fly.sh test //src/agent/tests:agent_network_test --test_output=all`

Expected: 6 tests PASS (新增 SubmitTask)

- [ ] **Step 4: Commit**

```bash
git add src/agent/cpp/master_agent.cpp src/agent/tests/agent_network_test.cpp
git commit -m "feat(agent): MasterAgent complete submit_task implementation

- assign_task_to_worker() sends TaskAssignMessage with metadata
- Update task status to RUNNING on assignment
- Add SubmitTask test for task submission flow

6 network tests PASS"
```

---

## Task 4: WorkerAgent 完整任务执行流程

**Files:**
- Modify: `src/agent/cpp/worker_agent.h`
- Modify: `src/agent/cpp/worker_agent.cpp`

- [ ] **Step 1: 修改 worker_agent.h 添加 running_tasks 跟踪**

```cpp
class WorkerAgent {
    // Phase 3: 任务状态跟踪
    CMVector<uint64_t> running_tasks_;
    
    // Phase 3: 任务完成报告
    void report_task_complete(uint64_t task_id);
    void report_task_failed(uint64_t task_id, const CMString& error);
};
```

- [ ] **Step 2: 修改 worker_agent.cpp 完善任务执行**

```cpp
void WorkerAgent::on_task_assign(const TaskAssignMessage& msg) {
    auto* log = Logger::get_worker(worker_id_);
    if (log) {
        log->info("WorkerAgent", "TaskAssign: task_id=" + std::to_string(msg.task_id));
    }
    
    running_tasks_.push_back(msg.task_id);
    
    if (executor_) {
        auto result = executor_->execute(
            msg.task_id, msg.task_name, msg.task_module, msg.args);
        
        running_tasks_.erase(
            std::remove(running_tasks_.begin(), running_tasks_.end(), msg.task_id),
            running_tasks_.end());
        
        if (result.status == TaskExecStatus::SUCCESS) {
            report_task_complete(msg.task_id);
        } else {
            report_task_failed(msg.task_id, result.error);
        }
    } else {
        if (log) {
            log->error("WorkerAgent", "no executor set");
        }
        report_task_failed(msg.task_id, "no executor configured");
    }
}

void WorkerAgent::report_task_complete(uint64_t task_id) {
    TaskCompleteMessage complete;
    complete.task_id = task_id;
    complete.worker_id = worker_id_;
    reactor_->send(master_conn_, complete);
    
    auto* log = Logger::get_worker(worker_id_);
    if (log) {
        log->info("WorkerAgent", "TaskComplete sent: task_id=" + std::to_string(task_id));
    }
}

void WorkerAgent::report_task_failed(uint64_t task_id, const CMString& error) {
    TaskFailedMessage failed;
    failed.task_id = task_id;
    failed.worker_id = worker_id_;
    failed.error_message = error;
    reactor_->send(master_conn_, failed);
    
    auto* log = Logger::get_worker(worker_id_);
    if (log) {
        log->error("WorkerAgent", "TaskFailed sent: task_id=" + std::to_string(task_id) + ", error=" + error);
    }
}
```

- [ ] **Step 3: 运行测试**

Run: `./fly.sh test //src/agent/tests:worker_agent_test --test_output=all`

Expected: 4 tests PASS

- [ ] **Step 4: Commit**

```bash
git add src/agent/cpp/worker_agent.h src/agent/cpp/worker_agent.cpp
git commit -m "feat(agent): WorkerAgent complete task execution flow

- Add running_tasks_ tracking for concurrent task execution
- Implement report_task_complete() and report_task_failed()
- Enhanced on_task_assign() with full execution flow
- Remove task from running_tasks_ after completion/failure

Phase 2 tests still pass"
```

---

## Task 5: 端到端集成测试

**Files:**
- Modify: `src/agent/tests/agent_network_test.cpp`

- [ ] **Step 1: 创建端到端任务提交→执行→完成测试**

```cpp
TEST_F(AgentNetworkTest, EndToEndTaskExecution) {
    // 启动 Master
    MasterAgent master("127.0.0.1", 19200);
    master.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // 启动 Worker，注入 mock executor
    WorkerAgent worker(1, "127.0.0.1", 19200);
    
    TaskExecutor executor;
    executor.set_exec_func([](uint64_t id, const CMString& name,
                              const CMString& module, const CMVector<CMString>& args) {
        TaskExecResult result;
        result.task_id = id;
        result.status = TaskExecStatus::SUCCESS;
        result.output = "executed: " + name;
        return result;
    });
    worker.set_executor(&executor);
    worker.start();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    EXPECT_TRUE(worker.is_registered());
    
    // 提交任务
    master.submit_task(1, "test_task", "test_module", {"arg1"}, {}, {});
    
    // 等待任务完成
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    auto completed = master.get_completed_tasks();
    EXPECT_EQ(completed.size(), 1);
    EXPECT_EQ(completed[0], 1);
    
    master.stop();
    worker.stop();
}

TEST_F(AgentNetworkTest, MultipleTaskExecution) {
    MasterAgent master("127.0.0.1", 19201);
    master.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    WorkerAgent worker1(1, "127.0.0.1", 19201);
    WorkerAgent worker2(2, "127.0.0.1", 19201);
    
    TaskExecutor executor;
    executor.set_exec_func([](uint64_t id, auto name, auto module, auto args) {
        TaskExecResult result;
        result.task_id = id;
        result.status = TaskExecStatus::SUCCESS;
        return result;
    });
    
    worker1.set_executor(&executor);
    worker2.set_executor(&executor);
    worker1.start();
    worker2.start();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    
    // 提交 3 个任务
    master.submit_task(1, "task1", "mod", {}, {}, {});
    master.submit_task(2, "task2", "mod", {}, {}, {});
    master.submit_task(3, "task3", "mod", {}, {}, {});
    
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    
    auto completed = master.get_completed_tasks();
    EXPECT_EQ(completed.size(), 3);
    
    master.stop();
    worker1.stop();
    worker2.stop();
}
```

- [ ] **Step 2: 运行端到端测试**

Run: `./fly.sh test //src/agent/tests:agent_network_test --test_output=all`

Expected: 8 tests PASS

- [ ] **Step 3: Commit**

```bash
git add src/agent/tests/agent_network_test.cpp
git commit -m "test(agent): Phase 3 end-to-end task execution tests

- EndToEndTaskExecution: submit → assign → execute → complete
- MultipleTaskExecution: 2 workers, 3 tasks concurrent execution

8 network tests PASS"
```

---

## Task 6: Python 导出和测试

**Files:**
- Modify: `src/agent/export/agent_export.cpp`
- Modify: `src/agent/tests/test_agent_integration.py`

- [ ] **Step 1: 更新 Python 导出**

```cpp
// agent_export.cpp
FLY_EXPORT_CLASS(fly::MasterAgent, "EXAgentMaster")
    FLY_EXPORT_INIT(fly::CMString, uint16_t)
    FLY_EXPORT_METHOD("start", &fly::MasterAgent::start)
    FLY_EXPORT_METHOD("stop", &fly::MasterAgent::stop)
    FLY_EXPORT_METHOD("is_running", &fly::MasterAgent::is_running)
    FLY_EXPORT_METHOD("get_connected_workers", &fly::MasterAgent::get_connected_workers)
    FLY_EXPORT_METHOD("get_connection_count", &fly::MasterAgent::get_connection_count)
    // Phase 3 新增
    FLY_EXPORT_METHOD("submit_task", [](fly::MasterAgent& self, uint64_t task_id,
                                         const fly::CMString& name,
                                         const fly::CMString& module,
                                         const fly::CMVector<fly::CMString>& args) {
        self.submit_task(task_id, name, module, args, {}, {});
    })
    FLY_EXPORT_METHOD("get_pending_tasks", &fly::MasterAgent::get_pending_tasks)
    FLY_EXPORT_METHOD("get_running_tasks", &fly::MasterAgent::get_running_tasks)
    FLY_EXPORT_METHOD("get_completed_tasks", &fly::MasterAgent::get_completed_tasks)
    FLY_EXPORT_METHOD("get_idle_workers", &fly::MasterAgent::get_idle_workers);
```

- [ ] **Step 2: 更新 Python 测试**

```python
def test_submit_task():
    log.init_master("test_logs/")
    log.init_worker(1, "test_logs/")
    
    master = agent.EXAgentMaster("127.0.0.1", 19300)
    master.start()
    time.sleep(0.1)
    
    worker = agent.EXAgentWorker(1, "127.0.0.1", 19300)
    
    executor = agent.EXTaskExecutor()
    executor.set_exec_func(lambda id, name, module, args: agent.EXTaskExecResult(
        task_id=id, status=agent.EXTaskExecStatus.SUCCESS, output="done"
    ))
    worker.set_executor(executor)
    worker.start()
    
    time.sleep(0.3)
    
    master.submit_task(1, "test_task", "test_module", [])
    time.sleep(0.5)
    
    completed = master.get_completed_tasks()
    assert len(completed) == 1
    assert completed[0] == 1
    
    master.stop()
    worker.stop()
    log.shutdown()
    print("PASS: test_submit_task")
```

- [ ] **Step 3: 运行 Python 测试**

Run: `python3 /root/fly/src/agent/tests/test_agent_integration.py`

Expected: 7 tests PASS

- [ ] **Step 4: Commit**

```bash
git add src/agent/export/agent_export.cpp src/agent/tests/test_agent_integration.py
git commit -m "feat(agent): Phase 3 Python exports and tests

- MasterAgent: submit_task, get_pending/running/completed_tasks
- WorkerAgent: existing exports sufficient
- Python test_submit_task for end-to-end flow

7 Python tests PASS"
```

---

## 实施顺序总结

```
Task 1: MasterAgent TaskScheduler/WorkerManager 集成
Task 2: MasterAgent MetadataManager/HeartbeatMonitor 集成
Task 3: MasterAgent submit_task() 完整实现
Task 4: WorkerAgent 完整任务执行流程
Task 5: 端到端集成测试
Task 6: Python 导出和测试
```

---

## 验证清单

实施完成后验证：

- [ ] Task 1: master_agent_test 3 tests pass
- [ ] Task 2: master_agent_test 3 tests pass
- [ ] Task 3: agent_network_test 6 tests pass
- [ ] Task 4: worker_agent_test 4 tests pass
- [ ] Task 5: agent_network_test 8 tests pass
- [ ] Task 6: test_agent_integration.py 7 tests pass
- [ ] All agent tests: `./fly.sh test //src/agent/...`
- [ ] 6 commits created