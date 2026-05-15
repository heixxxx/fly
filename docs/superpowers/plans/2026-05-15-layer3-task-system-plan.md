# Layer 3: Task System 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 实现分布式任务框架的任务调度系统，包括任务依赖图、Worker管理、元数据管理和任务调度器。

**Architecture:** 
- DependencyGraph 管理任务依赖关系，追踪数据就绪状态
- TaskScheduler 基于 FIFO + Locality 策略调度就绪任务到空闲 Worker
- WorkerManager 管理 Worker 注册、状态跟踪和属性管理
- MetadataManager 记录数据位置和任务元信息
- 所有组件通过消息协议与 Network Layer 交互

**Tech Stack:**
- C++20 (std::map, std::queue, std::shared_ptr)
- nanobind (Python 绑定）
- bitsery (通过 FLY_SERIALIZE 宏封装）
- gtest (单元测试）
- pytest (Python 集成测试）
- Bazel（构建系统）

---

## 文件结构

```
src/task/
├── cpp/
│   ├── dependency_graph.h         # 依赖图管理
│   ├── dependency_graph.cpp       # 依赖图实现
│   ├── task_scheduler.h           # 任务调度器
│   ├── task_scheduler.cpp         # 调度器实现
│   ├── metadata_manager.h         # 元数据管理
│   ├── metadata_manager.cpp       # 元数据实现
│   ├── worker_manager.h           # Worker管理
│   ├── worker_manager.cpp         # Worker管理实现
│   ├── heartbeat_monitor.h        # 心跳监控
│   ├── heartbeat_monitor.cpp      # 心跳实现
│   └── BUILD                      # cc_library targets
├── export/
│   ├── task_export.cpp            # nanobind 导出
│   └── BUILD                      # cc_binary: _fly_task.so
├── py/
│   ├── __init__.py                # from _fly_task import *
│   └── BUILD                      # py_library
└── tests/
    ├── dependency_graph_test.cpp  # 依赖图测试
    ├── task_scheduler_test.cpp    # 调度器测试
    ├── worker_manager_test.cpp    # Worker管理测试
    ├── task_test.py               # Python 集成测试
    └── BUILD                      # cc_test / py_test
```

---

## Task 1: DependencyGraph 依赖图

**Files:**
- Create: `src/task/cpp/dependency_graph.h`
- Create: `src/task/cpp/dependency_graph.cpp`
- Create: `src/task/tests/dependency_graph_test.cpp`
- Create: `src/task/cpp/BUILD`

- [ ] **Step 1: Write failing test**

```cpp
// src/task/tests/dependency_graph_test.cpp
#include <gtest/gtest.h>
#include <task/cpp/dependency_graph.h>

namespace fly {

TEST(DependencyGraphTest, AddTaskWithNoDependencies) {
    DependencyGraph graph;
    graph.add_task(1, {});
    auto ready = graph.get_ready_tasks();
    EXPECT_EQ(ready.size(), 1);
    EXPECT_EQ(ready[0], 1);
}

TEST(DependencyGraphTest, AddTaskWithDependencies) {
    DependencyGraph graph;
    graph.add_task(1, {"input/a"});
    graph.add_task(2, {"input/b"});
    
    auto ready = graph.get_ready_tasks();
    EXPECT_EQ(ready.size(), 0);
    
    graph.mark_data_ready("input/a");
    ready = graph.get_ready_tasks();
    EXPECT_EQ(ready.size(), 1);
    EXPECT_EQ(ready[0], 1);
}

TEST(DependencyGraphTest, MultipleDependencies) {
    DependencyGraph graph;
    graph.add_task(1, {"input/a", "input/b"});
    
    graph.mark_data_ready("input/a");
    EXPECT_FALSE(graph.is_task_ready(1));
    
    graph.mark_data_ready("input/b");
    EXPECT_TRUE(graph.is_task_ready(1));
    
    auto ready = graph.get_ready_tasks();
    EXPECT_EQ(ready.size(), 1);
}

TEST(DependencyGraphTest, RemoveTask) {
    DependencyGraph graph;
    graph.add_task(1, {});
    graph.remove_task(1);
    auto ready = graph.get_ready_tasks();
    EXPECT_EQ(ready.size(), 0);
}

TEST(DependencyGraphTest, CascadingDependencies) {
    DependencyGraph graph;
    graph.add_task(1, {});
    graph.add_task(2, {"output/1"});
    graph.add_task(3, {"output/2"});
    
    auto ready = graph.get_ready_tasks();
    EXPECT_EQ(ready.size(), 1);
    EXPECT_EQ(ready[0], 1);
    
    graph.remove_task(1);
    graph.mark_data_ready("output/1");
    
    ready = graph.get_ready_tasks();
    EXPECT_EQ(ready.size(), 1);
    EXPECT_EQ(ready[0], 2);
}

}  // namespace fly
```

- [ ] **Step 2: Run test to verify it fails**

Run: `./fly.sh test //src/task/tests:dependency_graph_test -v`
Expected: FAIL (files don't exist yet)

- [ ] **Step 3: Implement DependencyGraph header**

```cpp
// src/task/cpp/dependency_graph.h
#pragma once

#include <common/cpp/common_types.h>
#include <cstdint>
#include <map>
#include <set>
#include <vector>

namespace fly {

class DependencyGraph {
public:
    void add_task(uint64_t task_id, const CMVector<CMString>& inputs);
    void mark_data_ready(const CMString& data_path);
    CMVector<uint64_t> get_ready_tasks();
    bool is_task_ready(uint64_t task_id);
    void remove_task(uint64_t task_id);
    
private:
    CMMap<uint64_t, CMVector<CMString>> task_dependencies_;
    CMMap<CMString, bool> data_ready_status_;
    CMMap<uint64_t, int> pending_count_;
    CMSet<uint64_t> ready_tasks_;
    CMSet<uint64_t> completed_tasks_;
};

}  // namespace fly
```

- [ ] **Step 4: Implement DependencyGraph cpp**

```cpp
// src/task/cpp/dependency_graph.cpp
#include <task/cpp/dependency_graph.h>
#include <algorithm>

namespace fly {

void DependencyGraph::add_task(uint64_t task_id, const CMVector<CMString>& inputs) {
    task_dependencies_[task_id] = inputs;
    
    int pending = 0;
    for (const auto& dep : inputs) {
        if (!data_ready_status_.count(dep) || !data_ready_status_[dep]) {
            pending++;
        }
    }
    
    pending_count_[task_id] = pending;
    
    if (pending == 0) {
        ready_tasks_.insert(task_id);
    }
}

void DependencyGraph::mark_data_ready(const CMString& data_path) {
    data_ready_status_[data_path] = true;
    
    for (auto& [task_id, deps] : task_dependencies_) {
        if (completed_tasks_.count(task_id)) continue;
        if (ready_tasks_.count(task_id)) continue;
        
        bool all_ready = true;
        for (const auto& dep : deps) {
            if (!data_ready_status_.count(dep) || !data_ready_status_[dep]) {
                all_ready = false;
                break;
            }
        }
        
        if (all_ready) {
            ready_tasks_.insert(task_id);
        }
    }
}

CMVector<uint64_t> DependencyGraph::get_ready_tasks() {
    CMVector<uint64_t> result(ready_tasks_.begin(), ready_tasks_.end());
    return result;
}

bool DependencyGraph::is_task_ready(uint64_t task_id) {
    return ready_tasks_.count(task_id) > 0;
}

void DependencyGraph::remove_task(uint64_t task_id) {
    ready_tasks_.erase(task_id);
    completed_tasks_.insert(task_id);
    task_dependencies_.erase(task_id);
    pending_count_.erase(task_id);
}

}  // namespace fly
```

- [ ] **Step 5: Create BUILD file**

```python
# src/task/cpp/BUILD
package(default_visibility = ["//visibility:public"])

cc_library(
    name = "fly_task_dependency_graph",
    hdrs = ["dependency_graph.h"],
    srcs = ["dependency_graph.cpp"],
    deps = [
        "//src/common/cpp:fly_common_types",
    ],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
)

cc_library(
    name = "fly_task_cpp",
    deps = [
        ":fly_task_dependency_graph",
    ],
)
```

```python
# src/task/tests/BUILD
package(default_visibility = ["//visibility:public"])

cc_test(
    name = "dependency_graph_test",
    srcs = ["dependency_graph_test.cpp"],
    deps = [
        "//src/task/cpp:fly_task_dependency_graph",
        "@com_google_googletest//:gtest_main",
    ],
    copts = ["-std=c++20"],
)
```

- [ ] **Step 6: Run test to verify it passes**

Run: `./fly.sh test //src/task/tests:dependency_graph_test -v`
Expected: PASS

- [ ] **Step 7: Commit**

```bash
git add src/task/cpp/dependency_graph.h src/task/cpp/dependency_graph.cpp src/task/cpp/BUILD src/task/tests/dependency_graph_test.cpp src/task/tests/BUILD
git commit -m "feat: add DependencyGraph for task dependency management"
```

---

## Task 2: WorkerManager Worker管理

**Files:**
- Create: `src/task/cpp/worker_manager.h`
- Create: `src/task/cpp/worker_manager.cpp`
- Create: `src/task/tests/worker_manager_test.cpp`

- [ ] **Step 1: Write failing test**

```cpp
// src/task/tests/worker_manager_test.cpp
#include <gtest/gtest.h>
#include <task/cpp/worker_manager.h>

namespace fly {

TEST(WorkerManagerTest, RegisterWorker) {
    WorkerManager manager;
    manager.register_worker(1, "hybrid", {"gpu", "ssd"});
    
    auto worker = manager.get_worker(1);
    EXPECT_EQ(worker.worker_id, 1);
    EXPECT_EQ(worker.role, "hybrid");
    EXPECT_EQ(worker.attributes.size(), 2);
    EXPECT_FALSE(worker.is_busy);
}

TEST(WorkerManagerTest, GetAvailableWorkers) {
    WorkerManager manager;
    manager.register_worker(1, "hybrid", {});
    manager.register_worker(2, "hybrid", {});
    
    auto available = manager.get_available_workers();
    EXPECT_EQ(available.size(), 2);
    
    manager.mark_worker_busy(1, 100);
    available = manager.get_available_workers();
    EXPECT_EQ(available.size(), 1);
    EXPECT_EQ(available[0].worker_id, 2);
}

TEST(WorkerManagerTest, MarkWorkerBusyAndFree) {
    WorkerManager manager;
    manager.register_worker(1, "hybrid", {});
    
    manager.mark_worker_busy(1, 100);
    auto worker = manager.get_worker(1);
    EXPECT_TRUE(worker.is_busy);
    EXPECT_EQ(worker.current_task_id, 100);
    
    manager.mark_worker_free(1);
    worker = manager.get_worker(1);
    EXPECT_FALSE(worker.is_busy);
    EXPECT_EQ(worker.current_task_id, 0);
}

TEST(WorkerManagerTest, UpdateAttributes) {
    WorkerManager manager;
    manager.register_worker(1, "hybrid", {"gpu"});
    
    manager.update_attributes(1, {"ssd"}, {"gpu"});
    auto worker = manager.get_worker(1);
    EXPECT_EQ(worker.attributes.size(), 1);
    EXPECT_EQ(worker.attributes[0], "ssd");
}

TEST(WorkerManagerTest, GetWorkersByAttribute) {
    WorkerManager manager;
    manager.register_worker(1, "hybrid", {"gpu", "ssd"});
    manager.register_worker(2, "hybrid", {"gpu"});
    manager.register_worker(3, "storage_only", {"ssd"});
    
    auto gpu_workers = manager.get_workers_with_attribute("gpu");
    EXPECT_EQ(gpu_workers.size(), 2);
    
    auto ssd_workers = manager.get_workers_with_attribute("ssd");
    EXPECT_EQ(ssd_workers.size(), 2);
}

}  // namespace fly
```

- [ ] **Step 2: Run test to verify it fails**

Run: `./fly.sh test //src/task/tests:worker_manager_test -v`
Expected: FAIL

- [ ] **Step 3: Implement WorkerManager header**

```cpp
// src/task/cpp/worker_manager.h
#pragma once

#include <common/cpp/common_types.h>
#include <cstdint>
#include <map>
#include <vector>

namespace fly {

struct WorkerInfo {
    uint64_t worker_id = 0;
    CMString role;
    CMVector<CMString> attributes;
    bool is_busy = false;
    uint64_t current_task_id = 0;
    double last_heartbeat = 0;
};

class WorkerManager {
public:
    void register_worker(uint64_t worker_id, const CMString& role, const CMVector<CMString>& attributes = {});
    void update_attributes(uint64_t worker_id, const CMVector<CMString>& add_attrs, const CMVector<CMString>& remove_attrs = {});
    WorkerInfo get_worker(uint64_t worker_id);
    CMVector<WorkerInfo> get_available_workers();
    void mark_worker_busy(uint64_t worker_id, uint64_t task_id);
    void mark_worker_free(uint64_t worker_id);
    CMVector<WorkerInfo> get_workers_with_attribute(const CMString& attr);
    void update_heartbeat(uint64_t worker_id, double timestamp);
    
private:
    CMMap<uint64_t, WorkerInfo> workers_;
};

}  // namespace fly
```

- [ ] **Step 4: Implement WorkerManager cpp**

```cpp
// src/task/cpp/worker_manager.cpp
#include <task/cpp/worker_manager.h>
#include <algorithm>

namespace fly {

void WorkerManager::register_worker(uint64_t worker_id, const CMString& role, const CMVector<CMString>& attributes) {
    WorkerInfo info;
    info.worker_id = worker_id;
    info.role = role;
    info.attributes = attributes;
    info.is_busy = false;
    info.current_task_id = 0;
    info.last_heartbeat = 0;
    workers_[worker_id] = info;
}

void WorkerManager::update_attributes(uint64_t worker_id, const CMVector<CMString>& add_attrs, const CMVector<CMString>& remove_attrs) {
    auto it = workers_.find(worker_id);
    if (it == workers_.end()) return;
    
    auto& attrs = it->second.attributes;
    
    for (const auto& remove : remove_attrs) {
        attrs.erase(std::remove(attrs.begin(), attrs.end(), remove), attrs.end());
    }
    
    for (const auto& add : add_attrs) {
        if (std::find(attrs.begin(), attrs.end(), add) == attrs.end()) {
            attrs.push_back(add);
        }
    }
}

WorkerInfo WorkerManager::get_worker(uint64_t worker_id) {
    auto it = workers_.find(worker_id);
    if (it != workers_.end()) {
        return it->second;
    }
    return WorkerInfo{};
}

CMVector<WorkerInfo> WorkerManager::get_available_workers() {
    CMVector<WorkerInfo> available;
    for (const auto& [id, worker] : workers_) {
        if (!worker.is_busy) {
            available.push_back(worker);
        }
    }
    return available;
}

void WorkerManager::mark_worker_busy(uint64_t worker_id, uint64_t task_id) {
    auto it = workers_.find(worker_id);
    if (it != workers_.end()) {
        it->second.is_busy = true;
        it->second.current_task_id = task_id;
    }
}

void WorkerManager::mark_worker_free(uint64_t worker_id) {
    auto it = workers_.find(worker_id);
    if (it != workers_.end()) {
        it->second.is_busy = false;
        it->second.current_task_id = 0;
    }
}

CMVector<WorkerInfo> WorkerManager::get_workers_with_attribute(const CMString& attr) {
    CMVector<WorkerInfo> result;
    for (const auto& [id, worker] : workers_) {
        if (std::find(worker.attributes.begin(), worker.attributes.end(), attr) != worker.attributes.end()) {
            result.push_back(worker);
        }
    }
    return result;
}

void WorkerManager::update_heartbeat(uint64_t worker_id, double timestamp) {
    auto it = workers_.find(worker_id);
    if (it != workers_.end()) {
        it->second.last_heartbeat = timestamp;
    }
}

}  // namespace fly
```

- [ ] **Step 5: Update BUILD file**

```python
# Add to src/task/cpp/BUILD
cc_library(
    name = "fly_task_worker_manager",
    hdrs = ["worker_manager.h"],
    srcs = ["worker_manager.cpp"],
    deps = [
        "//src/common/cpp:fly_common_types",
    ],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
)

cc_library(
    name = "fly_task_cpp",
    deps = [
        ":fly_task_dependency_graph",
        ":fly_task_worker_manager",
    ],
)

# Add to src/task/tests/BUILD
cc_test(
    name = "worker_manager_test",
    srcs = ["worker_manager_test.cpp"],
    deps = [
        "//src/task/cpp:fly_task_worker_manager",
        "@com_google_googletest//:gtest_main",
    ],
    copts = ["-std=c++20"],
)
```

- [ ] **Step 6: Run test to verify it passes**

Run: `./fly.sh test //src/task/tests:worker_manager_test -v`
Expected: PASS

- [ ] **Step 7: Commit**

```bash
git add src/task/cpp/worker_manager.h src/task/cpp/worker_manager.cpp src/task/tests/worker_manager_test.cpp src/task/cpp/BUILD src/task/tests/BUILD
git commit -m "feat: add WorkerManager for worker registration and state tracking"
```

---

## Task 3: TaskScheduler 任务调度器

**Files:**
- Create: `src/task/cpp/task_scheduler.h`
- Create: `src/task/cpp/task_scheduler.cpp`
- Create: `src/task/tests/task_scheduler_test.cpp`

- [ ] **Step 1: Write failing test**

```cpp
// src/task/tests/task_scheduler_test.cpp
#include <gtest/gtest.h>
#include <task/cpp/task_scheduler.h>
#include <task/cpp/worker_manager.h>
#include <task/cpp/dependency_graph.h>

namespace fly {

TEST(TaskSchedulerTest, SubmitAndGetNextTask) {
    WorkerManager worker_mgr;
    DependencyGraph dep_graph;
    TaskScheduler scheduler(worker_mgr, dep_graph);
    
    worker_mgr.register_worker(1, "hybrid", {});
    
    TaskSubmitMessage msg;
    msg.task_id = 1;
    msg.task_name = "process_data";
    msg.task_module = "user_tasks";
    msg.inputs = {};
    
    scheduler.submit_task(msg);
    
    auto available = worker_mgr.get_available_workers();
    auto next = scheduler.get_next_task(available[0]);
    EXPECT_TRUE(next.has_value());
    EXPECT_EQ(next->task_id, 1);
}

TEST(TaskSchedulerTest, FIFOOrdering) {
    WorkerManager worker_mgr;
    DependencyGraph dep_graph;
    TaskScheduler scheduler(worker_mgr, dep_graph);
    
    worker_mgr.register_worker(1, "hybrid", {});
    
    for (int i = 1; i <= 3; i++) {
        TaskSubmitMessage msg;
        msg.task_id = i;
        msg.task_name = "task_" + std::to_string(i);
        msg.task_module = "user_tasks";
        msg.inputs = {};
        scheduler.submit_task(msg);
    }
    
    auto available = worker_mgr.get_available_workers();
    auto next = scheduler.get_next_task(available[0]);
    EXPECT_EQ(next->task_id, 1);
}

TEST(TaskSchedulerTest, LocalityPreference) {
    WorkerManager worker_mgr;
    DependencyGraph dep_graph;
    TaskScheduler scheduler(worker_mgr, dep_graph);
    
    worker_mgr.register_worker(1, "hybrid", {});
    worker_mgr.register_worker(2, "hybrid", {});
    
    TaskSubmitMessage msg1;
    msg1.task_id = 1;
    msg1.task_name = "task1";
    msg1.task_module = "user_tasks";
    msg1.inputs = {"data/a"};
    scheduler.submit_task(msg1);
    
    TaskSubmitMessage msg2;
    msg2.task_id = 2;
    msg2.task_name = "task2";
    msg2.task_module = "user_tasks";
    msg2.inputs = {};
    scheduler.submit_task(msg2);
    
    scheduler.record_data_location("data/a", 1);
    
    auto worker1 = worker_mgr.get_worker(1);
    auto next = scheduler.get_next_task(worker1);
    EXPECT_EQ(next->task_id, 1);
}

}  // namespace fly
```

- [ ] **Step 2: Run test to verify it fails**

Run: `./fly.sh test //src/task/tests:task_scheduler_test -v`
Expected: FAIL

- [ ] **Step 3: Implement TaskScheduler header**

```cpp
// src/task/cpp/task_scheduler.h
#pragma once

#include <common/cpp/common_types.h>
#include <task/cpp/dependency_graph.h>
#include <task/cpp/worker_manager.h>
#include <network/cpp/message_types.h>
#include <cstdint>
#include <map>
#include <optional>
#include <queue>

namespace fly {

struct TaskInfo {
    uint64_t task_id = 0;
    CMString task_name;
    CMString task_module;
    CMVector<CMString> args;
    CMVector<CMString> inputs;
    CMVector<CMString> required_attributes;
    int locality_score = 0;
};

class TaskScheduler {
public:
    TaskScheduler(WorkerManager& worker_mgr, DependencyGraph& dep_graph);
    
    void submit_task(const TaskSubmitMessage& msg);
    std::optional<TaskAssignMessage> get_next_task(const WorkerInfo& worker);
    bool has_ready_tasks() const;
    void record_data_location(const CMString& data_path, uint64_t worker_id);
    void mark_task_completed(uint64_t task_id);
    void mark_task_failed(uint64_t task_id);
    
private:
    WorkerManager& worker_mgr_;
    DependencyGraph& dep_graph_;
    
    CMMap<uint64_t, TaskInfo> pending_tasks_;
    CMQueue<uint64_t> ready_queue_;
    CMMap<CMString, CMVector<uint64_t>> data_locations_;
    
    int calculate_locality_score(const TaskInfo& task, const WorkerInfo& worker);
};

}  // namespace fly
```

- [ ] **Step 4: Implement TaskScheduler cpp**

```cpp
// src/task/cpp/task_scheduler.cpp
#include <task/cpp/task_scheduler.h>
#include <algorithm>

namespace fly {

TaskScheduler::TaskScheduler(WorkerManager& worker_mgr, DependencyGraph& dep_graph)
    : worker_mgr_(worker_mgr), dep_graph_(dep_graph) {
}

void TaskScheduler::submit_task(const TaskSubmitMessage& msg) {
    TaskInfo task;
    task.task_id = msg.header.message_id;
    task.task_name = msg.task_name;
    task.task_module = msg.task_module;
    task.inputs = msg.inputs;
    task.required_attributes = msg.required_attributes;
    
    pending_tasks_[task.task_id] = task;
    dep_graph_.add_task(task.task_id, task.inputs);
}

std::optional<TaskAssignMessage> TaskScheduler::get_next_task(const WorkerInfo& worker) {
    auto ready = dep_graph_.get_ready_tasks();
    
    if (ready.empty()) {
        return std::nullopt;
    }
    
    uint64_t best_task_id = 0;
    int best_score = -1;
    
    for (uint64_t task_id : ready) {
        auto it = pending_tasks_.find(task_id);
        if (it == pending_tasks_.end()) continue;
        
        int score = calculate_locality_score(it->second, worker);
        if (score > best_score) {
            best_score = score;
            best_task_id = task_id;
        }
    }
    
    if (best_task_id == 0) {
        return std::nullopt;
    }
    
    auto it = pending_tasks_.find(best_task_id);
    TaskAssignMessage assign_msg;
    assign_msg.header.type = MessageType::TASK_ASSIGN;
    assign_msg.header.message_id = best_task_id;
    assign_msg.task_id = best_task_id;
    assign_msg.task_name = it->second.task_name;
    assign_msg.task_module = it->second.task_module;
    assign_msg.args = it->second.args;
    
    pending_tasks_.erase(best_task_id);
    dep_graph_.remove_task(best_task_id);
    worker_mgr_.mark_worker_busy(worker.worker_id, best_task_id);
    
    return assign_msg;
}

bool TaskScheduler::has_ready_tasks() const {
    return !ready_queue_.empty();
}

void TaskScheduler::record_data_location(const CMString& data_path, uint64_t worker_id) {
    data_locations_[data_path].push_back(worker_id);
}

void TaskScheduler::mark_task_completed(uint64_t task_id) {
    pending_tasks_.erase(task_id);
}

void TaskScheduler::mark_task_failed(uint64_t task_id) {
    pending_tasks_.erase(task_id);
}

int TaskScheduler::calculate_locality_score(const TaskInfo& task, const WorkerInfo& worker) {
    if (task.inputs.empty()) return 0;
    
    int matched = 0;
    for (const auto& input : task.inputs) {
        auto it = data_locations_.find(input);
        if (it != data_locations_.end()) {
            for (uint64_t wid : it->second) {
                if (wid == worker.worker_id) {
                    matched++;
                    break;
                }
            }
        }
    }
    
    return matched * 100 / task.inputs.size();
}

}  // namespace fly
```

- [ ] **Step 5: Update BUILD file**

```python
# Add to src/task/cpp/BUILD
cc_library(
    name = "fly_task_scheduler",
    hdrs = ["task_scheduler.h"],
    srcs = ["task_scheduler.cpp"],
    deps = [
        ":fly_task_dependency_graph",
        ":fly_task_worker_manager",
        "//src/network/cpp:fly_network_message_types",
        "//src/common/cpp:fly_common_types",
    ],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
)

cc_library(
    name = "fly_task_cpp",
    deps = [
        ":fly_task_dependency_graph",
        ":fly_task_worker_manager",
        ":fly_task_scheduler",
    ],
)

# Add to src/task/tests/BUILD
cc_test(
    name = "task_scheduler_test",
    srcs = ["task_scheduler_test.cpp"],
    deps = [
        "//src/task/cpp:fly_task_scheduler",
        "//src/task/cpp:fly_task_worker_manager",
        "//src/task/cpp:fly_task_dependency_graph",
        "@com_google_googletest//:gtest_main",
    ],
    copts = ["-std=c++20"],
)
```

- [ ] **Step 6: Run test to verify it passes**

Run: `./fly.sh test //src/task/tests:task_scheduler_test -v`
Expected: PASS

- [ ] **Step 7: Commit**

```bash
git add src/task/cpp/task_scheduler.h src/task/cpp/task_scheduler.cpp src/task/tests/task_scheduler_test.cpp src/task/cpp/BUILD src/task/tests/BUILD
git commit -m "feat: add TaskScheduler with FIFO + Locality scheduling"
```

---

## Task 4: MetadataManager 元数据管理

**Files:**
- Create: `src/task/cpp/metadata_manager.h`
- Create: `src/task/cpp/metadata_manager.cpp`
- Create: `src/task/tests/metadata_manager_test.cpp`

- [ ] **Step 1: Write failing test**

```cpp
// src/task/tests/metadata_manager_test.cpp
#include <gtest/gtest.h>
#include <task/cpp/metadata_manager.h>

namespace fly {

TEST(MetadataManagerTest, RecordAndQueryData) {
    MetadataManager manager;
    
    DataLocation loc;
    loc.worker_id = 1;
    loc.file_path = "/data/file1.dat";
    loc.object_name = "output/result";
    
    manager.record_data(1, "output/result", loc);
    
    auto queried = manager.query_data("output/result");
    EXPECT_TRUE(queried.has_value());
    EXPECT_EQ(queried->worker_id, 1);
    EXPECT_EQ(queried->file_path, "/data/file1.dat");
}

TEST(MetadataManagerTest, QueryNonExistentData) {
    MetadataManager manager;
    auto result = manager.query_data("nonexistent");
    EXPECT_FALSE(result.has_value());
}

TEST(MetadataManagerTest, RecordTask) {
    MetadataManager manager;
    manager.record_task(1, "process_data", "user_tasks");
    
    auto task_name = manager.get_task_name(1);
    EXPECT_TRUE(task_name.has_value());
    EXPECT_EQ(task_name->name, "process_data");
    EXPECT_EQ(task_name->module, "user_tasks");
}

}  // namespace fly
```

- [ ] **Step 2: Run test to verify it fails**

Run: `./fly.sh test //src/task/tests:metadata_manager_test -v`
Expected: FAIL

- [ ] **Step 3: Implement MetadataManager**

```cpp
// src/task/cpp/metadata_manager.h
#pragma once

#include <common/cpp/common_types.h>
#include <network/cpp/message_types.h>
#include <cstdint>
#include <map>
#include <optional>

namespace fly {

struct DataLocation {
    uint64_t worker_id = 0;
    CMString file_path;
    CMString object_name;
};

struct TaskNameInfo {
    CMString name;
    CMString module;
};

class MetadataManager {
public:
    void record_data(uint64_t task_id, const CMString& path, const DataLocation& loc);
    std::optional<DataLocation> query_data(const CMString& path);
    void record_task(uint64_t task_id, const CMString& task_name, const CMString& task_module);
    std::optional<TaskNameInfo> get_task_name(uint64_t task_id);
    
private:
    CMMap<CMString, DataLocation> data_locations_;
    CMMap<uint64_t, TaskNameInfo> task_names_;
};

}  // namespace fly

// src/task/cpp/metadata_manager.cpp
#include <task/cpp/metadata_manager.h>

namespace fly {

void MetadataManager::record_data(uint64_t task_id, const CMString& path, const DataLocation& loc) {
    data_locations_[path] = loc;
}

std::optional<DataLocation> MetadataManager::query_data(const CMString& path) {
    auto it = data_locations_.find(path);
    if (it != data_locations_.end()) {
        return it->second;
    }
    return std::nullopt;
}

void MetadataManager::record_task(uint64_t task_id, const CMString& task_name, const CMString& task_module) {
    TaskNameInfo info;
    info.name = task_name;
    info.module = task_module;
    task_names_[task_id] = info;
}

std::optional<TaskNameInfo> MetadataManager::get_task_name(uint64_t task_id) {
    auto it = task_names_.find(task_id);
    if (it != task_names_.end()) {
        return it->second;
    }
    return std::nullopt;
}

}  // namespace fly
```

- [ ] **Step 4: Update BUILD and run tests**

```python
# Add to src/task/cpp/BUILD
cc_library(
    name = "fly_task_metadata_manager",
    hdrs = ["metadata_manager.h"],
    srcs = ["metadata_manager.cpp"],
    deps = [
        "//src/network/cpp:fly_network_message_types",
        "//src/common/cpp:fly_common_types",
    ],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
)

cc_library(
    name = "fly_task_cpp",
    deps = [
        ":fly_task_dependency_graph",
        ":fly_task_worker_manager",
        ":fly_task_scheduler",
        ":fly_task_metadata_manager",
    ],
)

# Add to src/task/tests/BUILD
cc_test(
    name = "metadata_manager_test",
    srcs = ["metadata_manager_test.cpp"],
    deps = [
        "//src/task/cpp:fly_task_metadata_manager",
        "@com_google_googletest//:gtest_main",
    ],
    copts = ["-std=c++20"],
)
```

Run: `./fly.sh test //src/task/tests:metadata_manager_test -v`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add src/task/cpp/metadata_manager.h src/task/cpp/metadata_manager.cpp src/task/tests/metadata_manager_test.cpp src/task/cpp/BUILD src/task/tests/BUILD
git commit -m "feat: add MetadataManager for data location and task metadata"
```

---

## Task 5: HeartbeatMonitor 心跳监控

**Files:**
- Create: `src/task/cpp/heartbeat_monitor.h`
- Create: `src/task/cpp/heartbeat_monitor.cpp`
- Create: `src/task/tests/heartbeat_monitor_test.cpp`

- [ ] **Step 1: Write failing test**

```cpp
// src/task/tests/heartbeat_monitor_test.cpp
#include <gtest/gtest.h>
#include <task/cpp/heartbeat_monitor.h>
#include <task/cpp/worker_manager.h>
#include <chrono>

namespace fly {

TEST(HeartbeatMonitorTest, UpdateAndCheckHeartbeat) {
    WorkerManager worker_mgr;
    worker_mgr.register_worker(1, "hybrid", {});
    
    HeartbeatMonitor monitor(worker_mgr, 5.0, 120.0);
    
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    monitor.update_heartbeat(1, static_cast<double>(now));
    
    auto timeout_workers = monitor.check_timeouts(static_cast<double>(now));
    EXPECT_EQ(timeout_workers.size(), 0);
}

TEST(HeartbeatMonitorTest, DetectTimeout) {
    WorkerManager worker_mgr;
    worker_mgr.register_worker(1, "hybrid", {});
    
    HeartbeatMonitor monitor(worker_mgr, 5.0, 120.0);
    
    double old_time = 1000.0;
    monitor.update_heartbeat(1, old_time);
    
    double current_time = 2000.0;
    auto timeout_workers = monitor.check_timeouts(current_time);
    EXPECT_EQ(timeout_workers.size(), 1);
    EXPECT_EQ(timeout_workers[0], 1);
}

}  // namespace fly
```

- [ ] **Step 2: Run test to verify it fails**

Run: `./fly.sh test //src/task/tests:heartbeat_monitor_test -v`
Expected: FAIL

- [ ] **Step 3: Implement HeartbeatMonitor**

```cpp
// src/task/cpp/heartbeat_monitor.h
#pragma once

#include <common/cpp/common_types.h>
#include <task/cpp/worker_manager.h>
#include <cstdint>
#include <vector>

namespace fly {

class HeartbeatMonitor {
public:
    HeartbeatMonitor(WorkerManager& worker_mgr, double interval, double timeout);
    
    void update_heartbeat(uint64_t worker_id, double timestamp);
    CMVector<uint64_t> check_timeouts(double current_time);
    double get_next_check_interval() const;
    
private:
    WorkerManager& worker_mgr_;
    double interval_;
    double timeout_;
};

}  // namespace fly

// src/task/cpp/heartbeat_monitor.cpp
#include <task/cpp/heartbeat_monitor.h>

namespace fly {

HeartbeatMonitor::HeartbeatMonitor(WorkerManager& worker_mgr, double interval, double timeout)
    : worker_mgr_(worker_mgr), interval_(interval), timeout_(timeout) {
}

void HeartbeatMonitor::update_heartbeat(uint64_t worker_id, double timestamp) {
    worker_mgr_.update_heartbeat(worker_id, timestamp);
}

CMVector<uint64_t> HeartbeatMonitor::check_timeouts(double current_time) {
    CMVector<uint64_t> timed_out;
    auto workers = worker_mgr_.get_available_workers();
    
    for (const auto& worker : workers) {
        if (worker.last_heartbeat > 0 && (current_time - worker.last_heartbeat) > timeout_) {
            timed_out.push_back(worker.worker_id);
        }
    }
    
    return timed_out;
}

double HeartbeatMonitor::get_next_check_interval() const {
    return interval_;
}

}  // namespace fly
```

- [ ] **Step 4: Update BUILD and run tests**

```python
# Add to src/task/cpp/BUILD
cc_library(
    name = "fly_task_heartbeat_monitor",
    hdrs = ["heartbeat_monitor.h"],
    srcs = ["heartbeat_monitor.cpp"],
    deps = [
        ":fly_task_worker_manager",
        "//src/common/cpp:fly_common_types",
    ],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
)

cc_library(
    name = "fly_task_cpp",
    deps = [
        ":fly_task_dependency_graph",
        ":fly_task_worker_manager",
        ":fly_task_scheduler",
        ":fly_task_metadata_manager",
        ":fly_task_heartbeat_monitor",
    ],
)

# Add to src/task/tests/BUILD
cc_test(
    name = "heartbeat_monitor_test",
    srcs = ["heartbeat_monitor_test.cpp"],
    deps = [
        "//src/task/cpp:fly_task_heartbeat_monitor",
        "//src/task/cpp:fly_task_worker_manager",
        "@com_google_googletest//:gtest_main",
    ],
    copts = ["-std=c++20"],
)
```

Run: `./fly.sh test //src/task/tests:heartbeat_monitor_test -v`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add src/task/cpp/heartbeat_monitor.h src/task/cpp/heartbeat_monitor.cpp src/task/tests/heartbeat_monitor_test.cpp src/task/cpp/BUILD src/task/tests/BUILD
git commit -m "feat: add HeartbeatMonitor for worker timeout detection"
```

---

## Task 6: Python 导出和集成测试

**Files:**
- Create: `src/task/export/task_export.cpp`
- Create: `src/task/export/BUILD`
- Create: `src/task/py/__init__.py`
- Create: `src/task/py/BUILD`
- Create: `src/task/tests/task_test.py`

- [ ] **Step 1: Write failing test**

```python
# src/task/tests/task_test.py
import pytest
import sys
import os

_bazel_bin = os.path.join(os.path.dirname(__file__), '..', 'bazel-bin', 'src', 'task', 'export')
if os.path.exists(_bazel_bin):
    sys.path.insert(0, _bazel_bin)

def test_import_task_module():
    from _fly_task import EXTaskDependencyGraph
    assert EXTaskDependencyGraph is not None

def test_dependency_graph_operations():
    from _fly_task import EXTaskDependencyGraph
    
    graph = EXTaskDependencyGraph()
    graph.add_task(1, [])
    
    ready = graph.get_ready_tasks()
    assert len(ready) == 1
    assert ready[0] == 1

def test_worker_manager_operations():
    from _fly_task import EXTaskWorkerManager
    
    manager = EXTaskWorkerManager()
    manager.register_worker(1, "hybrid", ["gpu", "ssd"])
    
    worker = manager.get_worker(1)
    assert worker.worker_id == 1
    assert worker.role == "hybrid"

if __name__ == "__main__":
    pytest.main([__file__, "-v"])
```

- [ ] **Step 2: Run test to verify it fails**

Run: `pytest src/task/tests/task_test.py -v`
Expected: FAIL

- [ ] **Step 3: Implement task_export.cpp**

```cpp
// src/task/export/task_export.cpp
#include <export/cpp/export_macros.h>
#include <serialization/cpp/serialization_macros.h>
#include <task/cpp/dependency_graph.h>
#include <task/cpp/worker_manager.h>
#include <task/cpp/task_scheduler.h>
#include <task/cpp/metadata_manager.h>
#include <task/cpp/heartbeat_monitor.h>
#include <memory>

FLY_EXPORT_MODULE(_fly_task) {

FLY_EXPORT_CLASS(fly::DependencyGraph, "EXTaskDependencyGraph")
    FLY_EXPORT_INIT()
    FLY_EXPORT_METHOD("add_task", &fly::DependencyGraph::add_task)
    FLY_EXPORT_METHOD("mark_data_ready", &fly::DependencyGraph::mark_data_ready)
    FLY_EXPORT_METHOD("get_ready_tasks", &fly::DependencyGraph::get_ready_tasks)
    FLY_EXPORT_METHOD("is_task_ready", &fly::DependencyGraph::is_task_ready)
    FLY_EXPORT_METHOD("remove_task", &fly::DependencyGraph::remove_task);

FLY_EXPORT_CLASS(fly::WorkerInfo, "EXTaskWorkerInfo")
    FLY_EXPORT_INIT()
    FLY_EXPORT_ATTR("worker_id", &fly::WorkerInfo::worker_id)
    FLY_EXPORT_ATTR("role", &fly::WorkerInfo::role)
    FLY_EXPORT_ATTR("attributes", &fly::WorkerInfo::attributes)
    FLY_EXPORT_ATTR("is_busy", &fly::WorkerInfo::is_busy)
    FLY_EXPORT_ATTR("current_task_id", &fly::WorkerInfo::current_task_id)
    FLY_EXPORT_ATTR("last_heartbeat", &fly::WorkerInfo::last_heartbeat);

FLY_EXPORT_CLASS(fly::WorkerManager, "EXTaskWorkerManager")
    FLY_EXPORT_INIT()
    FLY_EXPORT_METHOD("register_worker", &fly::WorkerManager::register_worker)
    FLY_EXPORT_METHOD("update_attributes", &fly::WorkerManager::update_attributes)
    FLY_EXPORT_METHOD("get_worker", &fly::WorkerManager::get_worker)
    FLY_EXPORT_METHOD("get_available_workers", &fly::WorkerManager::get_available_workers)
    FLY_EXPORT_METHOD("mark_worker_busy", &fly::WorkerManager::mark_worker_busy)
    FLY_EXPORT_METHOD("mark_worker_free", &fly::WorkerManager::mark_worker_free)
    FLY_EXPORT_METHOD("get_workers_with_attribute", &fly::WorkerManager::get_workers_with_attribute);

FLY_EXPORT_CLASS(fly::MetadataManager, "EXTaskMetadataManager")
    FLY_EXPORT_INIT()
    FLY_EXPORT_METHOD("record_data", &fly::MetadataManager::record_data)
    FLY_EXPORT_METHOD("query_data", &fly::MetadataManager::query_data)
    FLY_EXPORT_METHOD("record_task", &fly::MetadataManager::record_task)
    FLY_EXPORT_METHOD("get_task_name", &fly::MetadataManager::get_task_name);

FLY_EXPORT_CLASS(fly::HeartbeatMonitor, "EXTaskHeartbeatMonitor")
    FLY_EXPORT_INIT(fly::WorkerManager&, double, double)
    FLY_EXPORT_METHOD("update_heartbeat", &fly::HeartbeatMonitor::update_heartbeat)
    FLY_EXPORT_METHOD("check_timeouts", &fly::HeartbeatMonitor::check_timeouts)
    FLY_EXPORT_METHOD("get_next_check_interval", &fly::HeartbeatMonitor::get_next_check_interval);

}
```

- [ ] **Step 4: Create BUILD files**

```python
# src/task/export/BUILD
package(default_visibility = ["//visibility:public"])

cc_binary(
    name = "_fly_task.so",
    srcs = ["task_export.cpp"],
    deps = [
        "@nanobind//:nanobind_src",
        "//src/task/cpp:fly_task_cpp",
        "//src/export/cpp:fly_export_macros",
        "//src/serialization/cpp:fly_serialization_macros",
    ],
    copts = [
        "-std=c++20",
        "-I/usr/include/python3.10",
    ],
    linkopts = [
        "-lpython3.10",
        "-lpthread",
    ],
    linkshared = True,
    linkstatic = True,
)
```

```python
# src/task/py/BUILD
py_library(
    name = "fly_task_py",
    srcs = ["__init__.py"],
    deps = [],
    visibility = ["//visibility:public"],
)
```

```python
# Add to src/task/tests/BUILD
py_test(
    name = "task_test",
    srcs = ["task_test.py"],
    deps = [
        "//src/task/export:_fly_task.so",
    ],
    main = "task_test.py",
)
```

- [ ] **Step 5: Create Python __init__.py**

```python
# src/task/py/__init__.py
from _fly_task import (
    EXTaskDependencyGraph,
    EXTaskWorkerInfo,
    EXTaskWorkerManager,
    EXTaskMetadataManager,
    EXTaskHeartbeatMonitor,
)

__all__ = [
    'EXTaskDependencyGraph',
    'EXTaskWorkerInfo',
    'EXTaskWorkerManager',
    'EXTaskMetadataManager',
    'EXTaskHeartbeatMonitor',
]
```

- [ ] **Step 6: Run test to verify it passes**

Run: `./fly.sh test //src/task/tests:task_test -v`
Expected: PASS

- [ ] **Step 7: Commit**

```bash
git add src/task/export/task_export.cpp src/task/export/BUILD src/task/py/__init__.py src/task/py/BUILD src/task/tests/task_test.py src/task/tests/BUILD
git commit -m "feat: add Python exports for task system components"
```

---

## 实施顺序总结

```
Task 1: DependencyGraph       → Task 2: WorkerManager    → Task 3: TaskScheduler
                                      ↓
Task 4: MetadataManager       → Task 5: HeartbeatMonitor → Task 6: Python Export
```

---

## 预计完成时间

**预计时间**: 2-3 天（每个 Task 0.5 天，包括测试和构建调试）

**开始条件**: Layer 2 Networking 已完成并通过所有测试