# Layer 4 Phase 2 网络集成实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 Layer 2 Network (Reactor, TransportLayer) 集成到 MasterAgent 和 WorkerAgent，实现 Master/Worker TCP 通信。

**Architecture:** MasterAgent 拥有 Reactor TCP Server，维护 conn_id ↔ worker_id 双向映射；WorkerAgent 拥有 Reactor TCP Client，独立心跳线程，外部注入 TaskExecutor。

**Tech Stack:** C++20, Reactor, TransportLayer, MessageProtocol, nanobind (Python 绑定), gtest, pytest

---

## 文件结构

```
src/agent/
├── cpp/
│   ├── task_executor.h         # 修改: ExecFunc 签名修复 + set_exec_func()
│   ├── task_executor.cpp       # 修改: 实现修复
│   ├── master_agent.h          # 修改: 添加网络组件
│   ├── master_agent.cpp        # 修改: 实现 Reactor 集成
│   ├── worker_agent.h          # 修改: 添加网络组件 + executor 注入
│   ├── worker_agent.cpp        # 修改: 实现 Reactor 集成 + 心跳线程
│   └── BUILD                   # 修改: 添加 network 依赖
├── export/
│   ├── agent_export.cpp        # 修改: 添加新方法导出
│   └── BUILD                   # 保持不变
└── tests/
    ├── task_executor_test.cpp  # 修改: 更新 ExecFunc 签名
    ├── master_agent_test.cpp   # 保持不变 (Phase 1 测试)
    ├── worker_agent_test.cpp   # 保持不变 (Phase 1 测试)
    ├── agent_network_test.cpp  # 新建: 网络集成测试
    ├── test_agent_network.py   # 新建: Python 网络测试
    ├── test_agent_integration.py # 保持不变
    └── BUILD                   # 修改: 添加网络测试
```

---

## Task 1: 修复 TaskExecutor ExecFunc 签名并添加 set_exec_func()

**问题:** 当前 ExecFunc 缺少 `task_module` 参数，导致无法正确执行 Python 模块函数。

**Files:**
- Modify: `src/agent/cpp/task_executor.h`
- Modify: `src/agent/cpp/task_executor.cpp`
- Modify: `src/agent/tests/task_executor_test.cpp`

- [ ] **Step 1: 修改 task_executor.h 修复 ExecFunc 签名**

```cpp
// 第 24 行，修改 ExecFunc 签名
using ExecFunc = std::function<TaskExecResult(
    uint64_t task_id, 
    const CMString& task_name,
    const CMString& task_module,
    const CMVector<CMString>& args)>;

// 第 27-32 行，添加 set_exec_func 方法
class TaskExecutor {
public:
    using ExecFunc = std::function<TaskExecResult(
        uint64_t, const CMString&, const CMString&, const CMVector<CMString>&)>;
    
    TaskExecutor();
    explicit TaskExecutor(ExecFunc exec_func);
    
    void set_exec_func(ExecFunc exec_func);  // 新增
    
    TaskExecResult execute(uint64_t task_id, const CMString& task_name,
                           const CMString& task_module, const CMVector<CMString>& args);
    bool is_running() const;
    void cancel();
    
private:
    ExecFunc exec_func_;
    bool running_;
};
```

- [ ] **Step 2: 修改 task_executor.cpp 实现 set_exec_func**

```cpp
// 在第 7 行后添加
void TaskExecutor::set_exec_func(ExecFunc exec_func) {
    exec_func_ = std::move(exec_func);
}

// 第 17 行，修改 execute 中的 exec_func_ 调用
if (exec_func_) {
    result = exec_func_(task_id, task_name, task_module, args);
}
```

- [ ] **Step 3: 修改 task_executor_test.cpp 更新所有 lambda 签名**

```cpp
// 第 16-21 行，修改 CustomExecute 测试
TEST(TaskExecutorTest, CustomExecute) {
    TaskExecutor executor([](uint64_t task_id, const CMString& task_name,
                              const CMString& task_module, const CMVector<CMString>& args) {
        TaskExecResult result;
        result.task_id = task_id;
        result.status = TaskExecStatus::SUCCESS;
        result.output = "executed:" + task_name + ":" + task_module;
        return result;
    });
    
    auto result = executor.execute(42, "compute", "math", {});
    EXPECT_EQ(result.task_id, 42);
    EXPECT_EQ(result.output, "executed:compute:math");
}

// 第 30-35 行，修改 ExecuteWithArgs 测试
TEST(TaskExecutorTest, ExecuteWithArgs) {
    TaskExecutor executor([](uint64_t task_id, const CMString& task_name,
                              const CMString& task_module, const CMVector<CMString>& args) {
        TaskExecResult result;
        result.task_id = task_id;
        result.status = TaskExecStatus::SUCCESS;
        result.output = "args:" + std::to_string(args.size());
        return result;
    });
    
    auto result = executor.execute(1, "task", "mod", {"a", "b", "c"});
    EXPECT_EQ(result.output, "args:3");
}

// 第 43-48 行，修改 ExecuteFailure 测试
TEST(TaskExecutorTest, ExecuteFailure) {
    TaskExecutor executor([](uint64_t task_id, const CMString& task_name,
                              const CMString& task_module, const CMVector<CMString>& args) {
        TaskExecResult result;
        result.task_id = task_id;
        result.status = TaskExecStatus::FAILED;
        result.error = "segmentation fault";
        return result;
    });
    
    auto result = executor.execute(1, "bad_task", "mod", {});
    EXPECT_EQ(result.status, TaskExecStatus::FAILED);
    EXPECT_EQ(result.error, "segmentation fault");
}

// 新增 set_exec_func 测试
TEST(TaskExecutorTest, SetExecFunc) {
    TaskExecutor executor;
    
    executor.set_exec_func([](uint64_t task_id, const CMString& task_name,
                              const CMString& task_module, const CMVector<CMString>& args) {
        TaskExecResult result;
        result.task_id = task_id;
        result.status = TaskExecStatus::SUCCESS;
        result.output = "dynamic:" + task_name;
        return result;
    });
    
    auto result = executor.execute(1, "test", "module", {});
    EXPECT_EQ(result.output, "dynamic:test");
}
```

- [ ] **Step 4: 运行测试验证修改**

Run: `./fly.sh test //src/agent/tests:task_executor_test --test_output=all`

Expected: 7 tests PASS (原有 6 个 + 新增 1 个)

- [ ] **Step 5: Commit**

```bash
git add src/agent/cpp/task_executor.h src/agent/cpp/task_executor.cpp src/agent/tests/task_executor_test.cpp
git commit -m "fix(agent): TaskExecutor ExecFunc signature + set_exec_func method

- Fix ExecFunc signature to include task_module parameter
- Add set_exec_func() method for dynamic executor injection
- Update all test lambdas to match new signature
- Add SetExecFunc test for dynamic function setting"
```

---

## Task 2: MasterAgent 网络集成

**Files:**
- Modify: `src/agent/cpp/master_agent.h`
- Modify: `src/agent/cpp/master_agent.cpp`
- Modify: `src/agent/cpp/BUILD`

- [ ] **Step 1: 修改 master_agent.h 添加网络组件**

```cpp
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
    
    // Phase 2: 连接查询
    CMVector<uint64_t> get_connected_workers() const;
    size_t get_connection_count() const;
    
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
    
    // Phase 2: 消息处理
    void on_worker_register(uint64_t conn_id, const RegisterMessage& msg);
    void on_heartbeat(uint64_t conn_id, const HeartbeatMessage& msg);
    void on_disconnect(uint64_t conn_id);
    void on_error(uint64_t conn_id, int error_code);
};

}  // namespace fly
```

- [ ] **Step 2: 修改 master_agent.cpp 实现 Reactor 集成**

```cpp
#include <agent/cpp/master_agent.h>
#include <thread>
#include <chrono>

namespace fly {

MasterAgent::MasterAgent(const CMString& host, uint16_t port)
    : host_(host), port_(port), running_(false) {}

MasterAgent::~MasterAgent() {
    stop();
}

void MasterAgent::start() {
    if (running_) return;
    
    auto transport = create_transport("tcp");
    transport->listen(host_, port_);
    
    reactor_ = std::make_unique<Reactor>(std::move(transport));
    
    // 注册消息处理器
    reactor_->register_handler<RegisterMessage>(
        [this](uint64_t conn_id, const RegisterMessage& msg) {
            on_worker_register(conn_id, msg);
        });
    
    reactor_->register_handler<HeartbeatMessage>(
        [this](uint64_t conn_id, const HeartbeatMessage& msg) {
            on_heartbeat(conn_id, msg);
        });
    
    // 连接/断开事件
    reactor_->on_disconnect([this](uint64_t conn_id) {
        on_disconnect(conn_id);
    });
    
    reactor_->on_error([this](uint64_t conn_id, int err) {
        on_error(conn_id, err);
    });
    
    // 启动 Reactor 后台线程
    reactor_thread_ = std::thread([this] { reactor_->run(); });
    running_ = true;
}

void MasterAgent::stop() {
    if (running_) {
        reactor_->stop();
        if (reactor_thread_.joinable()) {
            reactor_thread_.join();
        }
        reactor_.reset();
        
        conn_to_worker_.clear();
        worker_to_conn_.clear();
        
        running_ = false;
    }
}

bool MasterAgent::is_running() const {
    return running_;
}

void MasterAgent::on_worker_register(uint64_t conn_id, const RegisterMessage& msg) {
    uint64_t worker_id = msg.worker_id;
    
    conn_to_worker_[conn_id] = worker_id;
    worker_to_conn_[worker_id] = conn_id;
    
    // 发送注册确认
    RegisterAckMessage ack;
    ack.worker_id = worker_id;
    ack.master_address = host_;
    ack.master_port = static_cast<int32_t>(port_);
    reactor_->send(conn_id, ack);
}

void MasterAgent::on_heartbeat(uint64_t conn_id, const HeartbeatMessage& msg) {
    // 心跳正常，连接存活
    // Phase 3 将集成 HeartbeatMonitor
}

void MasterAgent::on_disconnect(uint64_t conn_id) {
    auto it = conn_to_worker_.find(conn_id);
    if (it != conn_to_worker_.end()) {
        uint64_t worker_id = it->second;
        conn_to_worker_.erase(conn_id);
        worker_to_conn_.erase(worker_id);
    }
}

void MasterAgent::on_error(uint64_t conn_id, int error_code) {
    on_disconnect(conn_id);
}

CMVector<uint64_t> MasterAgent::get_connected_workers() const {
    CMVector<uint64_t> workers;
    for (const auto& [conn, worker] : conn_to_worker_) {
        workers.push_back(worker);
    }
    return workers;
}

size_t MasterAgent::get_connection_count() const {
    return conn_to_worker_.size();
}

}  // namespace fly
```

- [ ] **Step 3: 修改 BUILD 添加 network 依赖**

```python
# src/agent/cpp/BUILD 第 14-24 行修改
cc_library(
    name = "fly_agent_master",
    hdrs = ["master_agent.h"],
    srcs = ["master_agent.cpp"],
    deps = [
        ":fly_agent_task_executor",
        "//src/common/cpp:fly_common_types",
        "//src/network/cpp:fly_network_reactor",
        "//src/network/cpp:fly_network_transport",
    ],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
)
```

- [ ] **Step 4: 运行现有 MasterAgent 测试验证不破坏 Phase 1**

Run: `./fly.sh test //src/agent/tests:master_agent_test --test_output=all`

Expected: 3 tests PASS (CreateAndStart, CreateWithDifferentPorts, MultipleStartStop)

- [ ] **Step 5: Commit**

```bash
git add src/agent/cpp/master_agent.h src/agent/cpp/master_agent.cpp src/agent/cpp/BUILD
git commit -m "feat(agent): MasterAgent Phase 2 network integration

- Integrate Reactor TCP Server for Worker connections
- Add conn_to_worker_ / worker_to_conn_ dual-map
- Handle RegisterMessage, HeartbeatMessage, disconnect
- Send RegisterAckMessage on worker registration
- Add get_connected_workers() and get_connection_count()
- Phase 1 tests still pass"
```

---

## Task 3: WorkerAgent 网络集成

**Files:**
- Modify: `src/agent/cpp/worker_agent.h`
- Modify: `src/agent/cpp/worker_agent.cpp`
- Modify: `src/agent/cpp/BUILD`

- [ ] **Step 1: 修改 worker_agent.h 添加网络组件**

```cpp
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
    
    // Phase 2: TaskExecutor 注入
    void set_executor(TaskExecutor* executor);
    
    // Phase 2: 状态查询
    bool is_registered() const;
    
private:
    uint64_t worker_id_;
    CMString master_host_;
    uint16_t master_port_;
    std::atomic<bool> running_{false};
    std::atomic<bool> registered_{false};
    
    // Phase 2: 网络层
    std::unique_ptr<Reactor> reactor_;
    std::thread reactor_thread_;
    uint64_t master_conn_;
    
    // Phase 2: 心跳线程
    std::thread heartbeat_thread_;
    std::atomic<bool> heartbeat_running_{false};
    
    // Phase 2: TaskExecutor (外部)
    TaskExecutor* executor_{nullptr};
    
    // Phase 2: 消息处理
    void on_register_ack(const RegisterAckMessage& msg);
    void on_task_assign(const TaskAssignMessage& msg);
    void on_shutdown(const ShutdownMessage& msg);
    
    // Phase 2: 心跳发送
    void heartbeat_loop();
};

}  // namespace fly
```

- [ ] **Step 2: 修改 worker_agent.cpp 实现 Reactor 集成**

```cpp
#include <agent/cpp/worker_agent.h>
#include <thread>
#include <chrono>

namespace fly {

WorkerAgent::WorkerAgent(uint64_t worker_id, const CMString& master_host, uint16_t master_port)
    : worker_id_(worker_id), master_host_(master_host), master_port_(master_port),
      running_(false), registered_(false), executor_(nullptr) {}

WorkerAgent::~WorkerAgent() {
    stop();
}

void WorkerAgent::start() {
    if (running_) return;
    
    auto transport = create_transport("tcp");
    master_conn_ = transport->connect(master_host_, master_port_);
    
    reactor_ = std::make_unique<Reactor>(std::move(transport));
    
    // 注册消息处理器
    reactor_->register_handler<RegisterAckMessage>(
        [this](uint64_t conn, const RegisterAckMessage& msg) {
            on_register_ack(msg);
        });
    
    reactor_->register_handler<TaskAssignMessage>(
        [this](uint64_t conn, const TaskAssignMessage& msg) {
            on_task_assign(msg);
        });
    
    reactor_->register_handler<ShutdownMessage>(
        [this](uint64_t conn, const ShutdownMessage& msg) {
            on_shutdown(msg);
        });
    
    // Reactor 后台线程
    reactor_thread_ = std::thread([this] { reactor_->run(); });
    
    // 发送注册消息
    RegisterMessage reg;
    reg.worker_id = worker_id_;
    reactor_->send(master_conn_, reg);
    
    // 心跳线程
    heartbeat_running_ = true;
    heartbeat_thread_ = std::thread([this] { heartbeat_loop(); });
    
    running_ = true;
}

void WorkerAgent::stop() {
    if (running_) {
        heartbeat_running_ = false;
        if (heartbeat_thread_.joinable()) {
            heartbeat_thread_.join();
        }
        
        reactor_->stop();
        if (reactor_thread_.joinable()) {
            reactor_thread_.join();
        }
        reactor_.reset();
        
        running_ = false;
        registered_ = false;
    }
}

bool WorkerAgent::is_running() const {
    return running_;
}

uint64_t WorkerAgent::get_worker_id() const {
    return worker_id_;
}

void WorkerAgent::set_executor(TaskExecutor* executor) {
    executor_ = executor;
}

bool WorkerAgent::is_registered() const {
    return registered_;
}

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

void WorkerAgent::on_register_ack(const RegisterAckMessage& msg) {
    registered_ = true;
}

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

void WorkerAgent::on_shutdown(const ShutdownMessage& msg) {
    registered_ = false;
}

}  // namespace fly
```

- [ ] **Step 3: 修改 BUILD 添加 network 和 task_executor 依赖**

```python
# src/agent/cpp/BUILD 第 26-35 行修改
cc_library(
    name = "fly_agent_worker",
    hdrs = ["worker_agent.h"],
    srcs = ["worker_agent.cpp"],
    deps = [
        ":fly_agent_task_executor",
        "//src/common/cpp:fly_common_types",
        "//src/network/cpp:fly_network_reactor",
        "//src/network/cpp:fly_network_transport",
    ],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
)
```

- [ ] **Step 4: 运行现有 WorkerAgent 测试验证不破坏 Phase 1**

Run: `./fly.sh test //src/agent/tests:worker_agent_test --test_output=all`

Expected: 4 tests PASS (CreateAndGetId, StartStop, DifferentWorkerIds, MultipleStartStop)

- [ ] **Step 5: Commit**

```bash
git add src/agent/cpp/worker_agent.h src/agent/cpp/worker_agent.cpp src/agent/cpp/BUILD
git commit -m "feat(agent): WorkerAgent Phase 2 network integration

- Integrate Reactor TCP Client for Master connection
- Send RegisterMessage on start, handle RegisterAckMessage
- Independent heartbeat thread (10s interval)
- Handle TaskAssignMessage → executor->execute() → send result
- Handle ShutdownMessage
- Add set_executor() for external injection
- Add is_registered() state query
- Phase 1 tests still pass"
```

---

## Task 4: Python 导出更新

**Files:**
- Modify: `src/agent/export/agent_export.cpp`

- [ ] **Step 1: 修改 agent_export.cpp 添加新方法导出**

```cpp
// 在 FLY_EXPORT_CLASS(fly::MasterAgent, "EXAgentMaster") 部分
// 添加新的方法导出
FLY_EXPORT_CLASS(fly::MasterAgent, "EXAgentMaster")
    FLY_EXPORT_INIT(fly::CMString, uint16_t)
    FLY_EXPORT_METHOD("start", &fly::MasterAgent::start)
    FLY_EXPORT_METHOD("stop", &fly::MasterAgent::stop)
    FLY_EXPORT_METHOD("is_running", &fly::MasterAgent::is_running)
    FLY_EXPORT_METHOD("get_connected_workers", &fly::MasterAgent::get_connected_workers)
    FLY_EXPORT_METHOD("get_connection_count", &fly::MasterAgent::get_connection_count);

// 在 FLY_EXPORT_CLASS(fly::WorkerAgent, "EXAgentWorker") 部分
// 添加新的方法导出
FLY_EXPORT_CLASS(fly::WorkerAgent, "EXAgentWorker")
    FLY_EXPORT_INIT(uint64_t, fly::CMString, uint16_t)
    FLY_EXPORT_METHOD("start", &fly::WorkerAgent::start)
    FLY_EXPORT_METHOD("stop", &fly::WorkerAgent::stop)
    FLY_EXPORT_METHOD("is_running", &fly::WorkerAgent::is_running)
    FLY_EXPORT_METHOD("get_worker_id", &fly::WorkerAgent::get_worker_id)
    FLY_EXPORT_METHOD("set_executor", &fly::WorkerAgent::set_executor)
    FLY_EXPORT_METHOD("is_registered", &fly::WorkerAgent::is_registered);

// 在 FLY_EXPORT_CLASS(fly::TaskExecutor, "EXTaskExecutor") 部分
// 添加 set_exec_func 导出 (简化版，不支持 Python callable，仅用于 C++ 测试)
FLY_EXPORT_CLASS(fly::TaskExecutor, "EXTaskExecutor")
    FLY_EXPORT_INIT()
    FLY_EXPORT_METHOD("execute", [](fly::TaskExecutor& self, uint64_t task_id, 
                                     const fly::CMString& task_name, 
                                     const fly::CMString& task_module,
                                     const fly::CMVector<fly::CMString>& args) {
        return self.execute(task_id, task_name, task_module, args);
    })
    FLY_EXPORT_METHOD("is_running", &fly::TaskExecutor::is_running)
    FLY_EXPORT_METHOD("cancel", &fly::TaskExecutor::cancel);
    // Note: set_exec_func for Python callable requires nanobind wrapper
    // Will be added in Phase 3 when needed
```

- [ ] **Step 2: 构建并验证 Python 导出**

Run: `./fly.sh build //src/agent/export:_fly_agent.so`

Expected: Build successful, `_fly_agent.so` created

- [ ] **Step 3: Commit**

```bash
git add src/agent/export/agent_export.cpp
git commit -m "feat(agent): Python exports for Phase 2 methods

- MasterAgent: get_connected_workers, get_connection_count
- WorkerAgent: set_executor, is_registered
- TaskExecutor: execute with task_module parameter"
```

---

## Task 5: C++ 网络集成测试

**Files:**
- Create: `src/agent/tests/agent_network_test.cpp`
- Modify: `src/agent/tests/BUILD`

- [ ] **Step 1: 创建 agent_network_test.cpp**

```cpp
#include <gtest/gtest.h>
#include <agent/cpp/master_agent.h>
#include <agent/cpp/worker_agent.h>
#include <agent/cpp/task_executor.h>
#include <thread>
#include <chrono>

namespace fly {

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
    
    auto connected = master.get_connected_workers();
    EXPECT_EQ(connected.size(), 2);
    
    master.stop();
    worker1.stop();
    worker2.stop();
}

TEST(MasterWorkerNetworkTest, WorkerDisconnect) {
    MasterAgent master("127.0.0.1", 19082);
    master.start();
    
    WorkerAgent worker(1, "127.0.0.1", 19082);
    worker.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    EXPECT_EQ(master.get_connection_count(), 1);
    
    worker.stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    EXPECT_EQ(master.get_connection_count(), 0);
    
    master.stop();
}

TEST(MasterWorkerNetworkTest, ExecutorInjection) {
    TaskExecutor executor;
    executor.set_exec_func([](uint64_t id, const CMString& name,
                              const CMString& module, const CMVector<CMString>& args) {
        TaskExecResult result;
        result.task_id = id;
        result.status = TaskExecStatus::SUCCESS;
        result.output = "mock_result";
        return result;
    });
    
    auto result = executor.execute(1, "test_task", "test_module", {});
    EXPECT_EQ(result.status, TaskExecStatus::SUCCESS);
    EXPECT_EQ(result.output, "mock_result");
}

TEST(MasterWorkerNetworkTest, MasterRestart) {
    MasterAgent master("127.0.0.1", 19083);
    
    master.start();
    EXPECT_TRUE(master.is_running());
    master.stop();
    EXPECT_FALSE(master.is_running());
    
    master.start();
    EXPECT_TRUE(master.is_running());
    master.stop();
    EXPECT_FALSE(master.is_running());
}

}  // namespace fly
```

- [ ] **Step 2: 修改 tests/BUILD 添加网络测试**

```python
# src/agent/tests/BUILD 添加
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

- [ ] **Step 3: 运行网络测试**

Run: `./fly.sh test //src/agent/tests:agent_network_test --test_output=all`

Expected: 5 tests PASS

- [ ] **Step 4: Commit**

```bash
git add src/agent/tests/agent_network_test.cpp src/agent/tests/BUILD
git commit -m "test(agent): Phase 2 network integration tests

- WorkerRegister: Master accepts Worker connection
- MultipleWorkers: 2 workers connect to same master
- WorkerDisconnect: Master detects worker disconnect
- ExecutorInjection: set_exec_func mock execution
- MasterRestart: Master start/stop/restart cycle"
```

---

## Task 6: Python 网络集成测试

**Files:**
- Create: `src/agent/tests/test_agent_network.py`

- [ ] **Step 1: 创建 test_agent_network.py**

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

def test_master_restart():
    master = agent.EXAgentMaster("127.0.0.1", 19093)
    
    master.start()
    assert master.is_running() == True
    master.stop()
    assert master.is_running() == False
    
    master.start()
    assert master.is_running() == True
    master.stop()
    assert master.is_running() == False
    print("PASS: test_master_restart")

def test_executor_execute():
    executor = agent.EXTaskExecutor()
    result = executor.execute(1, "test_task", "test_module", ["arg1", "arg2"])
    
    assert result.task_id == 1
    assert result.status == agent.EXTaskExecStatus.SUCCESS
    print("PASS: test_executor_execute")

if __name__ == "__main__":
    test_master_worker_register()
    test_multiple_workers()
    test_worker_disconnect()
    test_master_restart()
    test_executor_execute()
    print("\nAll Python network tests passed!")
```

- [ ] **Step 2: 构建并运行 Python 测试**

Run: 
```bash
./fly.sh build //src/agent/export:_fly_agent.so
python3 /root/fly/src/agent/tests/test_agent_network.py
```

Expected: 5 tests PASS

- [ ] **Step 3: Commit**

```bash
git add src/agent/tests/test_agent_network.py
git commit -m "test(agent): Phase 2 Python network integration tests

- test_master_worker_register: Worker connects and registers
- test_multiple_workers: 2 workers connect to master
- test_worker_disconnect: Master detects worker disconnect
- test_master_restart: Master start/stop cycle
- test_executor_execute: TaskExecutor execute test"
```

---

## 实施顺序总结

```
Task 1: TaskExecutor 签名修复 + set_exec_func
Task 2: MasterAgent 网络集成 (Reactor Server)
Task 3: WorkerAgent 网络集成 (Reactor Client + 心跳)
Task 4: Python 导出更新
Task 5: C++ 网络集成测试
Task 6: Python 网络集成测试
```

---

## 验证清单

实施完成后验证：

- [ ] Task 1: TaskExecutor 7 tests pass
- [ ] Task 2: MasterAgent 3 Phase 1 tests pass
- [ ] Task 3: WorkerAgent 4 Phase 1 tests pass
- [ ] Task 4: _fly_agent.so builds successfully
- [ ] Task 5: agent_network_test 5 tests pass
- [ ] Task 6: test_agent_network.py 5 tests pass
- [ ] All agent tests pass: `./fly.sh test //src/agent/...`
- [ ] BUILD file updated correctly
- [ ] 6 commits created