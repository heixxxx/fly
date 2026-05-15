# Layer 4 Phase 2: Network Integration Design

**Date**: 2026-05-15
**Status**: Design Approved
**Dependencies**: Layer 2 Network (Reactor, TransportLayer, MessageProtocol), Layer 3 Task System

---

## 1. Overview

Phase 2 integrates Layer 2 Network components into MasterAgent and WorkerAgent, enabling real TCP communication between Master and Workers.

**Goals**:
- MasterAgent: TCP Server listening for Worker connections, handling Register/Heartbeat messages
- WorkerAgent: TCP Client connecting to Master, sending Register/Heartbeat, handling TaskAssign
- End-to-end network integration tests

**Not in Phase 2**:
- TaskScheduler/WorkerManager/HeartbeatMonitor integration (Phase 3)
- TaskExecutor execution logic (Phase 3)
- Python `launch_local_workers` / `launch_ssh_workers` (Phase 3)

---

## 2. Design Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Reactor ownership | MasterAgent owns Reactor | Single Master scenario, lifecycle clear |
| conn_id ↔ worker_id mapping | MasterAgent internal dual-map | No modification to Layer 3 WorkerManager |
| Heartbeat mechanism | WorkerAgent independent thread | Simple, no Reactor modification needed |
| TaskExecutor integration | External, injected to WorkerAgent | Python interpreter managed externally |

---

## 3. Architecture

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         Phase 2 Architecture                            │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│   ┌─────────────────────────────────────────────────────────────────┐   │
│   │                     MasterAgent                                  │   │
│   ├─────────────────────────────────────────────────────────────────┤   │
│   │  Components:                                                     │   │
│   │  - host_, port_, running_                                        │   │
│   │  - unique_ptr<Reactor> reactor_                                  │   │
│   │  - unique_ptr<TransportLayer> transport_                        │   │
│   │  - thread reactor_thread_                                        │   │
│   │  - conn_to_worker_, worker_to_conn_ (dual-map)                  │   │
│   │                                                                  │   │
│   │  Lifecycle:                                                      │   │
│   │  - start(): listen + register handlers + start reactor thread   │   │
│   │  - stop(): stop reactor + join thread + clear maps              │   │
│   │                                                                  │   │
│   │  Message Handlers:                                               │   │
│   │  - on_worker_register(conn, RegisterMessage)                    │   │
│   │  - on_heartbeat(conn, HeartbeatMessage)                         │   │
│   │  - on_disconnect(conn)                                          │   │
│   │  - on_error(conn, error_code)                                   │   │
│   │                                                                  │   │
│   │  New Methods:                                                    │   │
│   │  - get_connected_workers() → vector<worker_id>                  │   │
│   │  - get_connection_count() → size                                │   │
│   └─────────────────────────────────────────────────────────────────┘   │
│                              │                                          │
│                              │ TCP Network                              │
│                              ▼                                          │
│   ┌─────────────────────────────────────────────────────────────────┐   │
│   │                     WorkerAgent                                  │   │
│   ├─────────────────────────────────────────────────────────────────┤   │
│   │  Components:                                                     │   │
│   │  - worker_id_, master_host_, master_port_, running_             │   │
│   │  - unique_ptr<Reactor> reactor_                                  │   │
│   │  - unique_ptr<TransportLayer> transport_                        │   │
│   │  - thread reactor_thread_                                        │   │
│   │  - thread heartbeat_thread_                                      │   │
│   │  - ConnectionId master_conn_                                     │   │
│   │  - atomic<bool> registered_                                      │   │
│   │  - atomic<bool> heartbeat_running_                               │   │
│   │  - TaskExecutor* executor_ (external, injected)                  │   │
│   │                                                                  │   │
│   │  Lifecycle:                                                      │   │
│   │  - start(): connect + register handlers + send Register         │   │
│   │           + start reactor_thread + heartbeat_thread             │   │
│   │  - stop(): stop heartbeat + stop reactor + join threads         │   │
│   │                                                                  │   │
│   │  Message Handlers:                                               │   │
│   │  - on_register_ack(RegisterAckMessage) → set registered_        │   │
│   │  - on_task_assign(TaskAssignMessage) → executor->execute()      │   │
│   │  - on_shutdown(ShutdownMessage)                                 │   │
│   │                                                                  │   │
│   │  Heartbeat Thread:                                               │   │
│   │  - loop: sleep(10s) → send HeartbeatMessage → loop              │   │
│   │  - only sends if registered_ is true                            │   │
│   │                                                                  │   │
│   │  New Methods:                                                    │   │
│   │  - set_executor(TaskExecutor*) → inject executor                │   │
│   │  - is_registered() → bool                                       │   │
│   │  - is_connected() → bool                                        │   │
│   └─────────────────────────────────────────────────────────────────┘   │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## 4. MasterAgent Phase 2 Specification

### 4.1 Header Changes

```cpp
// src/agent/cpp/master_agent.h
#pragma once

#include <network/cpp/reactor.h>
#include <network/cpp/transport.h>
#include <network/cpp/message_types.h>
#include <common/cpp/common_types.h>
#include <cstdint>
#include <thread>
#include <atomic>
#include <map>

namespace fly {

class MasterAgent {
public:
    MasterAgent(const CMString& host, uint16_t port);
    ~MasterAgent();
    
    void start();
    void stop();
    bool is_running() const;
    
    // Phase 2: Connection queries
    CMVector<uint64_t> get_connected_workers() const;
    size_t get_connection_count() const;
    
private:
    CMString host_;
    uint16_t port_;
    std::atomic<bool> running_{false};
    
    // Phase 2: Network layer
    std::unique_ptr<Reactor> reactor_;
    std::thread reactor_thread_;
    
    // Phase 2: Connection mapping
    CMMap<uint64_t, uint64_t> conn_to_worker_;
    CMMap<uint64_t, uint64_t> worker_to_conn_;
    
    // Phase 2: Message handlers
    void on_worker_register(uint64_t conn_id, const RegisterMessage& msg);
    void on_heartbeat(uint64_t conn_id, const HeartbeatMessage& msg);
    void on_disconnect(uint64_t conn_id);
    void on_error(uint64_t conn_id, int error_code);
};

}  // namespace fly
```

### 4.2 Implementation Details

**start()**:
1. Create TCP TransportLayer
2. Call `transport->listen(host_, port_)`
3. Create Reactor with transport
4. Register message handlers (RegisterMessage, HeartbeatMessage)
5. Register event handlers (on_connect, on_disconnect, on_error)
6. Start reactor_thread running `reactor->run()`

**stop()**:
1. Call `reactor->stop()`
2. Join reactor_thread
3. Clear conn_to_worker_ and worker_to_conn_ maps
4. Set running_ = false

**on_worker_register(conn_id, msg)**:
1. Store `conn_to_worker_[conn_id] = msg.worker_id`
2. Store `worker_to_conn_[msg.worker_id] = conn_id`
3. Send RegisterAckMessage back to conn_id

**on_disconnect(conn_id)**:
1. Look up worker_id from conn_to_worker_
2. Remove both entries from dual-map
3. (Phase 3 will integrate WorkerManager status update)

---

## 5. WorkerAgent Phase 2 Specification

### 5.1 Header Changes

```cpp
// src/agent/cpp/worker_agent.h
#pragma once

#include <network/cpp/reactor.h>
#include <network/cpp/transport.h>
#include <network/cpp/message_types.h>
#include <agent/cpp/task_executor.h>
#include <common/cpp/common_types.h>
#include <cstdint>
#include <thread>
#include <atomic>

namespace fly {

class WorkerAgent {
public:
    WorkerAgent(uint64_t worker_id, const CMString& master_host, uint16_t master_port);
    ~WorkerAgent();
    
    void start();
    void stop();
    bool is_running() const;
    uint64_t get_worker_id() const;
    
    // Phase 2: TaskExecutor injection
    void set_executor(TaskExecutor* executor);
    
    // Phase 2: State queries
    bool is_registered() const;
    
private:
    uint64_t worker_id_;
    CMString master_host_;
    uint16_t master_port_;
    std::atomic<bool> running_{false};
    std::atomic<bool> registered_{false};
    
    // Phase 2: Network layer
    std::unique_ptr<Reactor> reactor_;
    std::thread reactor_thread_;
    uint64_t master_conn_;
    
    // Phase 2: Heartbeat thread
    std::thread heartbeat_thread_;
    std::atomic<bool> heartbeat_running_{false};
    
    // Phase 2: TaskExecutor (external)
    TaskExecutor* executor_{nullptr};
    
    // Phase 2: Message handlers
    void on_register_ack(const RegisterAckMessage& msg);
    void on_task_assign(const TaskAssignMessage& msg);
    void on_shutdown(const ShutdownMessage& msg);
    
    // Phase 2: Heartbeat loop
    void heartbeat_loop();
};

}  // namespace fly
```

### 5.2 Implementation Details

**start()**:
1. Create TCP TransportLayer
2. Call `transport->connect(master_host_, master_port_)` → get master_conn_
3. Create Reactor with transport
4. Register message handlers (RegisterAckMessage, TaskAssignMessage, ShutdownMessage)
5. Start reactor_thread running `reactor->run()`
6. Send RegisterMessage with worker_id_
7. Start heartbeat_thread running `heartbeat_loop()`

**stop()**:
1. Set heartbeat_running_ = false
2. Join heartbeat_thread
3. Call `reactor->stop()`
4. Join reactor_thread
5. Set registered_ = false, running_ = false

**heartbeat_loop()**:
```cpp
void WorkerAgent::heartbeat_loop() {
    while (heartbeat_running_) {
        std::this_thread::sleep_for(std::chrono::seconds(10));
        if (registered_ && heartbeat_running_) {
            HeartbeatMessage hb;
            hb.worker_id = worker_id_;
            reactor_->send(master_conn_, hb);
        }
    }
}
```

**on_task_assign(msg)**:
```cpp
void WorkerAgent::on_task_assign(const TaskAssignMessage& msg) {
    if (executor_) {
        auto result = executor_->execute(
            msg.task_id, msg.task_name, msg.task_module, msg.args);
        
        if (result.status == TaskExecStatus::SUCCESS) {
            TaskCompleteMessage complete;
            complete.task_id = msg.task_id;
            complete.worker_id = worker_id_;
            reactor_->send(master_conn_, complete);
        } else {
            TaskFailedMessage failed;
            failed.task_id = msg.task_id;
            failed.worker_id = worker_id_;
            failed.error_message = result.error;
            reactor_->send(master_conn_, failed);
        }
    }
}
```

---

## 6. TaskExecutor Phase 2 Changes

TaskExecutor needs `set_exec_func()` method for testing and Python injection:

```cpp
// src/agent/cpp/task_executor.h additions
class TaskExecutor {
public:
    // Phase 2: Custom exec function for testing/Python
    using ExecFunc = std::function<TaskExecResult(
        uint64_t task_id, const CMString& name,
        const CMString& module, const CMVector<CMString>& args)>;
    
    void set_exec_func(ExecFunc func);
    
private:
    ExecFunc custom_exec_func_;
};
```

---

## 7. Python Export Updates

### 7.1 MasterAgent Python Export

```cpp
// src/agent/export/agent_export.cpp - MasterAgent updates
FLY_EXPORT_CLASS(fly::MasterAgent, "EXAgentMaster")
    FLY_EXPORT_INIT(fly::CMString, uint16_t)
    FLY_EXPORT_METHOD("start", &fly::MasterAgent::start)
    FLY_EXPORT_METHOD("stop", &fly::MasterAgent::stop)
    FLY_EXPORT_METHOD("is_running", &fly::MasterAgent::is_running)
    // Phase 2 new methods
    FLY_EXPORT_METHOD("get_connected_workers", &fly::MasterAgent::get_connected_workers)
    FLY_EXPORT_METHOD("get_connection_count", &fly::MasterAgent::get_connection_count);
```

### 7.2 WorkerAgent Python Export

```cpp
// src/agent/export/agent_export.cpp - WorkerAgent updates
FLY_EXPORT_CLASS(fly::WorkerAgent, "EXAgentWorker")
    FLY_EXPORT_INIT(uint64_t, fly::CMString, uint16_t)
    FLY_EXPORT_METHOD("start", &fly::WorkerAgent::start)
    FLY_EXPORT_METHOD("stop", &fly::WorkerAgent::stop)
    FLY_EXPORT_METHOD("is_running", &fly::WorkerAgent::is_running)
    FLY_EXPORT_METHOD("get_worker_id", &fly::WorkerAgent::get_worker_id)
    // Phase 2 new methods
    FLY_EXPORT_METHOD("set_executor", &fly::WorkerAgent::set_executor)
    FLY_EXPORT_METHOD("is_registered", &fly::WorkerAgent::is_registered);
```

### 7.3 TaskExecutor Python Export

```cpp
// src/agent/export/agent_export.cpp - TaskExecutor updates
FLY_EXPORT_CLASS(fly::TaskExecutor, "EXTaskExecutor")
    FLY_EXPORT_INIT()
    FLY_EXPORT_METHOD("execute", [](fly::TaskExecutor& self, uint64_t task_id, 
                                     const fly::CMString& name, 
                                     const fly::CMString& module,
                                     const fly::CMVector<fly::CMString>& args) {
        return self.execute(task_id, name, module, args);
    })
    FLY_EXPORT_METHOD("is_running", &fly::TaskExecutor::is_running)
    FLY_EXPORT_METHOD("cancel", &fly::TaskExecutor::cancel)
    // Phase 2 new method - set_exec_func accepts Python callable
    // Note: Python callable wrapping is complex, defer to implementation phase
```

---

## 8. Test Strategy

### 8.1 C++ Integration Tests

**Test File**: `src/agent/tests/agent_network_test.cpp`

```cpp
// Test 1: Basic Worker registration
TEST(MasterWorkerNetworkTest, WorkerRegister) {
    MasterAgent master("127.0.0.1", 19080);
    master.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    WorkerAgent worker(1, "127.0.0.1", 19080);
    worker.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    EXPECT_TRUE(worker.is_registered());
    EXPECT_EQ(master.get_connection_count(), 1);
    
    auto connected = master.get_connected_workers();
    EXPECT_EQ(connected.size(), 1);
    EXPECT_EQ(connected[0], 1);
    
    master.stop();
    worker.stop();
}

// Test 2: Multiple Workers
TEST(MasterWorkerNetworkTest, MultipleWorkers) {
    MasterAgent master("127.0.0.1", 19081);
    master.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    WorkerAgent worker1(1, "127.0.0.1", 19081);
    WorkerAgent worker2(2, "127.0.0.1", 19081);
    worker1.start();
    worker2.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    
    EXPECT_EQ(master.get_connection_count(), 2);
    
    master.stop();
    worker1.stop();
    worker2.stop();
}

// Test 3: Heartbeat flow
TEST(MasterWorkerNetworkTest, HeartbeatFlow) {
    MasterAgent master("127.0.0.1", 19082);
    master.start();
    
    WorkerAgent worker(1, "127.0.0.1", 19082);
    worker.start();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_TRUE(worker.is_registered());
    
    // Wait for heartbeat (10s interval)
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Master should still have connection
    EXPECT_EQ(master.get_connection_count(), 1);
    
    master.stop();
    worker.stop();
}

// Test 4: Worker disconnect detection
TEST(MasterWorkerNetworkTest, WorkerDisconnect) {
    MasterAgent master("127.0.0.1", 19083);
    master.start();
    
    WorkerAgent worker(1, "127.0.0.1", 19083);
    worker.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    EXPECT_EQ(master.get_connection_count(), 1);
    
    worker.stop();  // Worker disconnects
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Master should detect disconnect
    EXPECT_EQ(master.get_connection_count(), 0);
    
    master.stop();
}

// Test 5: TaskAssign with mock executor (Phase 3 will complete)
// Note: MasterAgent needs send_task_assign() method which requires
// TaskScheduler integration. This test skeleton is for Phase 3.
TEST(MasterWorkerNetworkTest, TaskAssignWithMockExecutor_Skeleton) {
    // Phase 2: Just verify executor injection works
    TaskExecutor executor;
    executor.set_exec_func([](uint64_t id, auto name, auto module, auto args) {
        TaskExecResult result;
        result.task_id = id;
        result.status = TaskExecStatus::SUCCESS;
        result.output = "mock_result";
        return result;
    });
    
    // Verify executor is callable
    auto result = executor.execute(1, "test_task", "test_module", {});
    EXPECT_EQ(result.status, TaskExecStatus::SUCCESS);
    EXPECT_EQ(result.output, "mock_result");
    
    // Full network test in Phase 3 when MasterAgent can send TaskAssign
}
```

### 8.2 Python Integration Tests

**Test File**: `src/agent/tests/test_agent_network.py`

```python
import sys
import os
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 
                                '../../../bazel-bin/src/agent/export'))

import _fly_agent as agent

def test_master_worker_register():
    master = agent.EXAgentMaster("127.0.0.1", 19090)
    master.start()
    time.sleep(0.1)
    
    worker = agent.EXAgentWorker(1, "127.0.0.1", 19090)
    worker.start()
    time.sleep(0.2)
    
    assert worker.is_registered() == True
    assert master.get_connection_count() == 1
    
    connected = master.get_connected_workers()
    assert len(connected) == 1
    assert connected[0] == 1
    
    master.stop()
    worker.stop()
    print("PASS: test_master_worker_register")

def test_multiple_workers():
    master = agent.EXAgentMaster("127.0.0.1", 19091)
    master.start()
    time.sleep(0.1)
    
    worker1 = agent.EXAgentWorker(1, "127.0.0.1", 19091)
    worker2 = agent.EXAgentWorker(2, "127.0.0.1", 19091)
    worker1.start()
    worker2.start()
    time.sleep(0.3)
    
    assert master.get_connection_count() == 2
    
    master.stop()
    worker1.stop()
    worker2.stop()
    print("PASS: test_multiple_workers")

def test_worker_disconnect():
    master = agent.EXAgentMaster("127.0.0.1", 19092)
    master.start()
    
    worker = agent.EXAgentWorker(1, "127.0.0.1", 19092)
    worker.start()
    time.sleep(0.2)
    
    assert master.get_connection_count() == 1
    
    worker.stop()
    time.sleep(0.2)
    
    assert master.get_connection_count() == 0
    
    master.stop()
    print("PASS: test_worker_disconnect")

if __name__ == "__main__":
    test_master_worker_register()
    test_multiple_workers()
    test_worker_disconnect()
    print("\nAll network tests passed!")
```

---

## 9. Build Dependencies

### 9.1 MasterAgent BUILD

```python
# src/agent/cpp/BUILD - update fly_agent_master
cc_library(
    name = "fly_agent_master",
    hdrs = ["master_agent.h"],
    srcs = ["master_agent.cpp"],
    deps = [
        ":fly_agent_task_executor",
        "//src/common/cpp:fly_common_types",
        "//src/network/cpp:fly_network_reactor",  # Phase 2 new
        "//src/network/cpp:fly_network_transport",  # Phase 2 new
    ],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
)
```

### 9.2 WorkerAgent BUILD

```python
# src/agent/cpp/BUILD - update fly_agent_worker
cc_library(
    name = "fly_agent_worker",
    hdrs = ["worker_agent.h"],
    srcs = ["worker_agent.cpp"],
    deps = [
        ":fly_agent_task_executor",  # Phase 2 new
        "//src/common/cpp:fly_common_types",
        "//src/network/cpp:fly_network_reactor",  # Phase 2 new
        "//src/network/cpp:fly_network_transport",  # Phase 2 new
    ],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
)
```

### 9.3 Test BUILD

```python
# src/agent/tests/BUILD - add network test
cc_test(
    name = "agent_network_test",
    srcs = ["agent_network_test.cpp"],
    deps = [
        "//src/agent/cpp:fly_agent_cpp",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
    copts = ["-std=c++20"],
)
```

---

## 10. Implementation Order

```
Task 1: TaskExecutor set_exec_func() method
Task 2: MasterAgent network integration (Reactor, handlers, dual-map)
Task 3: WorkerAgent network integration (Reactor, handlers, heartbeat thread)
Task 4: Python export updates
Task 5: C++ network integration tests
Task 6: Python network integration tests
```

---

## 11. Risks and Mitigations

| Risk | Mitigation |
|------|------------|
| Reactor thread safety for send() | TransportLayer send() must be thread-safe; test with concurrent sends |
| Heartbeat thread blocking on stop() | Use atomic flag + join timeout |
| Worker disconnect not detected quickly | Reactor on_disconnect handler fires immediately |
| Port conflicts in tests | Use distinct ports per test (19080-19099 range) |

---

## 12. Success Criteria

- MasterAgent starts TCP Server, accepts Worker connections
- WorkerAgent connects to Master, sends Register, receives RegisterAck
- WorkerAgent heartbeat thread sends periodic HeartbeatMessage
- MasterAgent tracks connected workers via dual-map
- Disconnect events properly update connection state
- All C++ and Python network tests pass
- Build completes without errors