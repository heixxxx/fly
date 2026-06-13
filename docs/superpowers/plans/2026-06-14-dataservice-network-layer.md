# DataService 独立网络层实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use omo-subagent-driven-development (recommended) or omo-dispatching-parallel-agents to implement this plan task-by-task. Each task should specify a `category` (quick/deep/ultrabrain/visual-engineering) and `load_skills` for oh-my-opencode's task() tool. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** DataService 拥有独立的 listen 端口和 accept/recv/send 线程，DATA_REQUEST/DATA_RESPONSE 完全脱离 reactor transport，避免大数据传输阻塞 reactor。

**Architecture:** DataService 新增 data server 模块（listen socket + accept 线程 + IO 线程池）。DataClient 连接的目标端口从 reactor 端口变为 DataService 端口。Agent 启动时先启动 reactor（控制端口），再启动 DataService data server（数据端口），注册给 Master 的是数据端口。

**Tech Stack:** C++20, gcc12, POSIX sockets, epoll, Bazel, CM* type aliases

---

## 当前架构（改造前）

```
Worker::start()
  transport->listen("0.0.0.0", 0)         → 端口 P（reactor transport，控制+数据共用）
  data_server_port_ = P
  DataService::start_transfer_server()    → 只启动 IOThreadPool，无 listen socket
  reactor->register_handler<DATA_REQUEST> → reactor 处理 DATA_REQUEST

DataClient → 连接端口 P → reactor epoll → on_data_request → submit_transfer
  → IO 线程读取已压缩数据
  → completion 在 reactor 线程 → reactor_->send(DataResponse) → 阻塞 reactor
```

## 目标架构（改造后）

```
Worker::start()
  transport->listen("0.0.0.0", 0)             → 端口 C（reactor，仅控制消息）
  DataService::start_data_server(host, 0)      → 端口 D（独立 listen + accept 线程）
  data_server_port_ = D
  reactor 不再注册 DATA_REQUEST handler

DataClient → 连接端口 D → DataService accept 线程 → IO 线程
  → recv DataRequest → 读取已压缩数据 → send DataResponse → close fd
  → 完全不经 reactor
```

---

## File Structure

| 文件 | 操作 | 职责 |
|------|------|------|
| `src/storage/cpp/data_server.h` | 新建 | DataServer 类声明：listen + accept + IO 处理 |
| `src/storage/cpp/data_server.cpp` | 新建 | DataServer 实现 |
| `src/storage/cpp/data_service.h` | 修改 | 新增 `start_data_server()` / `stop_data_server()` / `get_data_port()`，移除 transfer callback 机制 |
| `src/storage/cpp/data_service.cpp` | 修改 | 新增 data_server_ 成员，start/stop 委托给 DataServer，移除 submit_transfer / transfer_callback_ / transfer_pool_ |
| `src/storage/cpp/BUILD` | 修改 | 新增 data_server.cpp 到 srcs |
| `src/agent/cpp/master_agent.cpp` | 修改 | start() 中启动 DataService data server，data_server_port_ 改为 DataService 端口，移除 DATA_REQUEST handler 和 transfer callback |
| `src/agent/cpp/worker_agent.cpp` | 修改 | 同 master_agent |
| `src/network/cpp/reactor.h` | 修改 | 移除 io_pool_ / set_io_pool / process_completions 相关（reactor 不再处理 IO completion） |
| `src/network/cpp/reactor.cpp` | 修改 | 移除 run() 中 process_completions 调用 |

---

### Task 1: 创建 DataServer 类

**Files:**
- Create: `src/storage/cpp/data_server.h`
- Create: `src/storage/cpp/data_server.cpp`

- [ ] **Step 1: 创建 data_server.h**

```cpp
#pragma once

#include <common/cpp/common_types.h>
#include <cstdint>
#include <atomic>
#include <thread>
#include <functional>

namespace fly {

class DataService;

class DataServer {
public:
    DataServer(DataService& ds, int io_thread_count);
    ~DataServer();

    void start(const CMString& host, int port);
    void stop();

    int get_port() const { return data_port_; }

private:
    DataService& data_service_;
    int io_thread_count_;

    int listen_fd_ = -1;
    int data_port_ = 0;

    std::atomic<bool> running_{false};
    std::thread accept_thread_;

    struct IOWorker;
    CMVector<std::unique_ptr<IOWorker>> io_workers_;
    std::atomic<uint64_t> next_worker_{0};

    void accept_loop();
    void handle_connection(int fd);
};

}  // namespace fly
```

- [ ] **Step 2: 创建 data_server.cpp — accept 线程骨架**

```cpp
#include <storage/cpp/data_server.h>
#include <storage/cpp/data_service.h>
#include <network/cpp/message_protocol.h>
#include <network/cpp/message_types.h>
#include <network/cpp/net_utils.h>
#include <log/cpp/logger.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <poll.h>

namespace fly {

DataServer::DataServer(DataService& ds, int io_thread_count)
    : data_service_(ds), io_thread_count_(io_thread_count) {}

DataServer::~DataServer() {
    stop();
}

void DataServer::start(const CMString& host, int port) {
    listen_fd_ = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (listen_fd_ < 0) {
        ERR("DataServer: failed to create listen socket: {}", std::strerror(errno));
        return;
    }

    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(host.c_str());
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (bind(listen_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        ERR("DataServer: bind failed: {}", std::strerror(errno));
        ::close(listen_fd_);
        listen_fd_ = -1;
        return;
    }

    if (::listen(listen_fd_, 128) < 0) {
        ERR("DataServer: listen failed: {}", std::strerror(errno));
        ::close(listen_fd_);
        listen_fd_ = -1;
        return;
    }

    struct sockaddr_in bound_addr;
    socklen_t bound_len = sizeof(bound_addr);
    getsockname(listen_fd_, reinterpret_cast<struct sockaddr*>(&bound_addr), &bound_len);
    data_port_ = ntohs(bound_addr.sin_port);

    running_ = true;
    accept_thread_ = std::thread(&DataServer::accept_loop, this);

    INFO("DataServer listening on port {}", data_port_);
}

void DataServer::stop() {
    if (!running_.exchange(false)) return;

    if (listen_fd_ >= 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }

    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }
}

void DataServer::accept_loop() {
    while (running_) {
        struct pollfd pfd;
        pfd.fd = listen_fd_;
        pfd.events = POLLIN;
        int ret = ::poll(&pfd, 1, 100);
        if (ret <= 0) continue;

        int fd = ::accept4(listen_fd_, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
            if (!running_) return;
            ERR("DataServer: accept failed: {}", std::strerror(errno));
            continue;
        }

        int nodelay = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

        handle_connection(fd);
    }
}

}  // namespace fly
```

- [ ] **Step 3: 实现 handle_connection — recv DataRequest → 读数据 → send DataResponse → close**

在 data_server.cpp 中追加：

```cpp
void DataServer::handle_connection(int fd) {
    struct timeval tv;
    tv.tv_sec = 30;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    char header[5] = {};
    if (!net_recv_exact(fd, header, 5, 30000)) {
        ::close(fd);
        return;
    }

    uint32_t total_len =
        (static_cast<uint32_t>(static_cast<unsigned char>(header[0])) << 24) |
        (static_cast<uint32_t>(static_cast<unsigned char>(header[1])) << 16) |
        (static_cast<uint32_t>(static_cast<unsigned char>(header[2])) << 8) |
        static_cast<uint32_t>(static_cast<unsigned char>(header[3]));

    if (total_len < 1) {
        ::close(fd);
        return;
    }

    uint32_t payload_len = total_len - 1;
    CMString payload(payload_len, '\0');
    if (payload_len > 0 && !net_recv_exact(fd, payload.data(), payload_len, 30000)) {
        ::close(fd);
        return;
    }

    CMString full_buf;
    full_buf.resize(4 + total_len);
    std::memcpy(&full_buf[0], header, 5);
    if (payload_len > 0) {
        std::memcpy(&full_buf[5], payload.data(), payload_len);
    }

    DataRequestMessage req;
    if (!MessageProtocol::decode(full_buf, req)) {
        ::close(fd);
        return;
    }

    DBG("DataServer: DataRequest for object={}", req.object_name_);

    DataResponseMessage response;
    response.object_name_ = req.object_name_;

    auto [found, raw_data, py_name] = data_service_.try_read_local_raw_or_wait(req.object_name_, -1);

    if (found) {
        response.success_ = true;
        response.compressed_data_ = std::move(raw_data);
        response.py_name_ = std::move(py_name);

        auto write_hash = data_service_.get_write_context_hash(req.object_name_);
        if (!write_hash.empty()) {
            response.write_context_hash_ = write_hash;
        }
    } else {
        response.success_ = false;
        response.error_message_ = "Object not found: " + req.object_name_;
    }

    CMString resp_frame = MessageProtocol::encode(response);
    if (!net_send_all(fd, resp_frame.data(), resp_frame.size(), 30000)) {
        ERR("DataServer: failed to send response for {}", req.object_name_);
    }

    ::close(fd);
}

}  // namespace fly
```

注意：`try_read_local_raw_or_wait` 和 `get_write_context_hash` 是 DataService 的 public 方法。需要确认 `get_write_context_hash` 是否已存在，如果没有则需要在 DataService 中添加。

- [ ] **Step 4: 更新 BUILD**

在 `src/storage/cpp/BUILD` 的 `fly_storage` cc_library 中，`srcs` 已使用 `glob(["*.cpp"])`，新文件自动包含。确认 `data_server.cpp` 的 include 依赖 `//src/network/cpp:fly_network`（已有依赖）。

无需修改 BUILD。

- [ ] **Step 5: 构建验证**

```bash
./fly.sh buildonly //src/storage/cpp:fly_storage
```

Expected: 编译通过（可能有 LSP 误报 `-fno-canonical-system-headers`，忽略）。

- [ ] **Step 6: Commit**

```bash
git add src/storage/cpp/data_server.h src/storage/cpp/data_server.cpp
git commit -m "feat: add DataServer class with independent listen/accept/handle"
```

---

### Task 2: DataService 集成 DataServer

**Files:**
- Modify: `src/storage/cpp/data_service.h`
- Modify: `src/storage/cpp/data_service.cpp`

- [ ] **Step 1: 检查 try_read_local_raw_or_wait 的访问权限**

```bash
grep -n 'try_read_local_raw_or_wait' src/storage/cpp/data_service.h
```

确认它是 public 方法。如果不是，改为 public。

- [ ] **Step 2: 添加 get_write_context_hash 到 DataService**

在 `data_service.h` 的 public 区域添加：

```cpp
CMString get_write_context_hash(const CMString& object_name) const;
```

在 `data_service.cpp` 中实现：

```cpp
CMString DataService::get_write_context_hash(const CMString& object_name) const {
    auto [db_id, short_name] = split_full(object_name);
    std::lock_guard<std::mutex> lock(mutex_);
    auto db_it = local_idx_.find(db_id);
    if (db_it != local_idx_.end()) {
        auto it = db_it->second.find(short_name);
        if (it != db_it->second.end() && it->second && !it->second->entries_.empty()) {
            return it->second->entries_.back().write_context_hash_;
        }
    }
    return {};
}
```

- [ ] **Step 3: 在 DataService 中新增 data_server 相关接口**

在 `data_service.h` 中：

1. 新增前向声明和成员：
```cpp
class DataServer;  // 前向声明

// 在 private 成员区域添加：
CMUniquePtr<DataServer> data_server_;
```

2. 在 public 区域添加：
```cpp
// ============================================================
// Data Server (独立数据传输网络层)
// ============================================================

void start_data_server(const CMString& host, int port, int io_thread_count);
void stop_data_server();
int get_data_port() const;
```

3. 移除旧的 transfer server 接口（标记 deprecated 或直接删除）：
```cpp
// 移除以下接口：
// void start_transfer_server(int thread_count, TransferCallback callback);
// void stop_transfer_server();
// bool is_transfer_server_running() const;
// void submit_transfer(uint64_t conn_id, const CMString& object_name, ...);
// CMSharedPtr<IOThreadPool> get_transfer_pool() const;
```

- [ ] **Step 4: 在 data_service.cpp 中实现新接口**

```cpp
#include <storage/cpp/data_server.h>

void DataService::start_data_server(const CMString& host, int port, int io_thread_count) {
    data_server_ = CMMakeUnique<DataServer>(*this, io_thread_count);
    data_server_->start(host, port);
}

void DataService::stop_data_server() {
    if (data_server_) {
        data_server_->stop();
        data_server_.reset();
    }
}

int DataService::get_data_port() const {
    if (data_server_) {
        return data_server_->get_port();
    }
    return 0;
}
```

移除旧的 `start_transfer_server` / `stop_transfer_server` / `submit_transfer` / `get_transfer_pool` 实现。

移除成员变量：`transfer_pool_`, `transfer_callback_`, `transfer_running_`, `active_transfers_`。

- [ ] **Step 5: 更新析构函数**

在 `DataService::~DataService()` 中：
```cpp
DataService::~DataService() {
    if (data_server_) {
        data_server_->stop();
    }
    if (write_back_queue_) {
        write_back_queue_->drain();
        write_back_queue_->stop();
    }
}
```

移除 `stop_transfer_server()` 调用。

- [ ] **Step 6: 构建验证**

```bash
./fly.sh buildonly //src/storage/cpp:fly_storage
```

Expected: 编译通过。

- [ ] **Step 7: Commit**

```bash
git add src/storage/cpp/data_service.h src/storage/cpp/data_service.cpp
git commit -m "feat: integrate DataServer into DataService, remove transfer callback mechanism"
```

---

### Task 3: WorkerAgent 接入 DataService Data Server

**Files:**
- Modify: `src/agent/cpp/worker_agent.cpp`
- Modify: `src/agent/cpp/worker_agent.h`

- [ ] **Step 1: 修改 WorkerAgent::start()**

当前代码（worker_agent.cpp:26-62）需要改造：

**旧代码：**
```cpp
auto transport = create_transport("tcp");
transport->listen("0.0.0.0", 0);
data_server_port_ = static_cast<int32_t>(transport->get_bound_port());
data_server_host_ = ProcessInfo::instance()->data_server_host();
INFO("data server listening on port {}", data_server_port_);
master_conn_ = transport->connect(master_host_, master_port_);
reactor_ = CMMakeUnique<Reactor>(std::move(transport));

auto dsInst = DataService::instance();
int data_server_threads = static_cast<int>(Config::instance()->get_int("data_server_threads"));
dsInst->start_transfer_server(
    data_server_threads,
    [this](const TransferResult& result) {
        DataResponseMessage response;
        response.object_name_ = result.object_name_;
        response.success_ = result.success_;
        response.compressed_data_ = result.compressed_data_;
        response.py_name_ = result.py_name_;
        response.write_context_hash_ = result.write_context_hash_;
        if (!result.success_) {
            response.error_message_ = result.error_message_;
        }
        reactor_->send(result.conn_id_, response);
    });
reactor_->set_io_pool(dsInst->get_transfer_pool());
```

**新代码：**
```cpp
auto transport = create_transport("tcp");
transport->listen("0.0.0.0", 0);
master_conn_ = transport->connect(master_host_, master_port_);
reactor_ = CMMakeUnique<Reactor>(std::move(transport));

data_server_host_ = ProcessInfo::instance()->data_server_host();
auto dsInst = DataService::instance();
int data_server_threads = static_cast<int>(Config::instance()->get_int("data_server_threads"));
dsInst->start_data_server(data_server_host_, 0, data_server_threads);
data_server_port_ = static_cast<int32_t>(dsInst->get_data_port());
INFO("data server listening on port {}", data_server_port_);
```

关键变化：
- transport 不再 listen 用于 data server（transport listen 只用于 reactor 控制连接）
- `data_server_port_` 改为 DataService 的端口
- 移除 `start_transfer_server` + transfer callback
- 移除 `reactor_->set_io_pool()`

- [ ] **Step 2: 移除 DATA_REQUEST handler 注册**

在 worker_agent.cpp 中找到并删除：
```cpp
reactor_->register_handler<DataRequestMessage>(
    [this](uint64_t conn_id, const DataRequestMessage& msg) {
        on_data_request(conn_id, msg);
    });
```

DATA_REQUEST 不再经过 reactor。

- [ ] **Step 3: 修改 do_cleanup()**

当前代码（worker_agent.cpp:186-200）：
```cpp
void WorkerAgent::do_cleanup() {
    data_client_pool_.stop();
    if (heartbeat_thread_.joinable()) {
        heartbeat_thread_.join();
    }
    if (reactor_thread_.joinable()) {
        reactor_thread_.join();
    }
    reactor_.reset();
    databases_.clear();
    DataService::instance()->stop_transfer_server();
    ...
}
```

改为：
```cpp
void WorkerAgent::do_cleanup() {
    data_client_pool_.stop();
    if (heartbeat_thread_.joinable()) {
        heartbeat_thread_.join();
    }
    if (reactor_thread_.joinable()) {
        reactor_thread_.join();
    }
    reactor_.reset();
    databases_.clear();
    DataService::instance()->stop_data_server();
    ...
}
```

- [ ] **Step 4: 构建验证**

```bash
./fly.sh buildonly //src/agent/cpp:fly_agent
```

Expected: 编译通过（可能有 on_data_request 未使用警告，后续清理）。

- [ ] **Step 5: Commit**

```bash
git add src/agent/cpp/worker_agent.cpp src/agent/cpp/worker_agent.h
git commit -m "feat: WorkerAgent uses DataService data server instead of reactor transport"
```

---

### Task 4: MasterAgent 接入 DataService Data Server

**Files:**
- Modify: `src/agent/cpp/master_agent.cpp`
- Modify: `src/agent/cpp/master_agent.h`

- [ ] **Step 1: 修改 MasterAgent::start()**

当前代码（master_agent.cpp:37-190）需要改造。

在 reactor 创建之后、register_worker 之前，添加 DataService data server 启动：

**旧代码（master_agent.cpp:170-190）：**
```cpp
data_server_port_ = static_cast<int32_t>(port_);
DataService::instance()->register_worker(0, host_, port_);

auto dsInst = DataService::instance();
int data_server_threads = static_cast<int>(Config::instance()->get_int("data_server_threads"));
dsInst->start_transfer_server(
    data_server_threads,
    [this](const TransferResult& result) {
        DataResponseMessage response;
        ...
        reactor_->send(result.conn_id_, response);
    });
reactor_->set_io_pool(dsInst->get_transfer_pool());
```

**新代码：**
```cpp
auto dsInst = DataService::instance();
int data_server_threads = static_cast<int>(Config::instance()->get_int("data_server_threads"));
dsInst->start_data_server(host_, 0, data_server_threads);
data_server_port_ = static_cast<int32_t>(dsInst->get_data_port());
DataService::instance()->register_worker(0, host_, data_server_port_);
```

- [ ] **Step 2: 移除 MasterAgent 的 DATA_REQUEST handler**

在 master_agent.cpp 中找到并删除：
```cpp
reactor_->register_handler<DataRequestMessage>(
    [this](uint64_t conn_id, const DataRequestMessage& msg) {
        on_data_request(conn_id, msg);
    });
```

- [ ] **Step 3: 修改 MasterAgent::stop() 中的清理**

在所有调用 `stop_transfer_server()` 的地方改为 `stop_data_server()`。

搜索：
```bash
grep -n 'stop_transfer_server' src/agent/cpp/master_agent.cpp
```

全部替换为 `stop_data_server()`。

- [ ] **Step 4: 移除 reactor->set_io_pool 调用**

搜索：
```bash
grep -n 'set_io_pool\|get_transfer_pool' src/agent/cpp/master_agent.cpp
```

删除所有 `reactor_->set_io_pool(...)` 调用。

- [ ] **Step 5: 构建验证**

```bash
./fly.sh build //src/main/cpp:fly
```

Expected: 编译通过。

- [ ] **Step 6: Commit**

```bash
git add src/agent/cpp/master_agent.cpp src/agent/cpp/master_agent.h
git commit -m "feat: MasterAgent uses DataService data server, removes DATA_REQUEST from reactor"
```

---

### Task 5: Reactor 移除 IOThreadPool 依赖

**Files:**
- Modify: `src/network/cpp/reactor.h`
- Modify: `src/network/cpp/reactor.cpp`

- [ ] **Step 1: 移除 reactor.h 中的 io_pool 相关代码**

在 reactor.h 中删除：
- `#include <network/cpp/io_thread_pool.h>` （如果 reactor.h 不再需要它）
- `CMSharedPtr<IOThreadPool> io_pool_;` 成员
- `void set_io_pool(CMSharedPtr<IOThreadPool> pool);` 方法
- `CMSharedPtr<IOThreadPool> get_io_pool() const { return io_pool_; }` 方法

- [ ] **Step 2: 移除 reactor.cpp 中的 io_pool 相关代码**

在 reactor.cpp 中删除：
- `set_io_pool` 实现
- `run()` 中的 `if (io_pool_) { io_pool_->process_completions(); }` 调用

- [ ] **Step 3: 构建验证**

```bash
./fly.sh build //src/main/cpp:fly
```

Expected: 编译通过。如果有其他地方调用 `reactor_->set_io_pool` 或 `reactor_->get_io_pool`，需要一并清理。

- [ ] **Step 4: Commit**

```bash
git add src/network/cpp/reactor.h src/network/cpp/reactor.cpp
git commit -m "refactor: remove IOThreadPool dependency from Reactor"
```

---

### Task 6: 清理废弃代码

**Files:**
- Modify: `src/agent/cpp/master_agent.cpp` — 移除 `on_data_request` 方法
- Modify: `src/agent/cpp/worker_agent.cpp` — 移除 `on_data_request` 方法
- Modify: `src/storage/cpp/data_service.h` — 移除 `TransferCallback` typedef，移除 `TransferResult` struct（如果不再使用）
- Modify: `src/storage/cpp/data_service.cpp` — 移除 `submit_transfer` 实现

- [ ] **Step 1: 搜索并移除 on_data_request**

```bash
grep -rn 'on_data_request' src/agent/
```

在 master_agent.cpp/h 和 worker_agent.cpp/h 中删除 `on_data_request` 方法声明和实现。

- [ ] **Step 2: 搜索并移除 submit_transfer**

```bash
grep -rn 'submit_transfer' src/
```

如果只在 data_service.cpp 中有定义（已在 Task 2 中移除），跳过。如果还有调用方残留，清理。

- [ ] **Step 3: 搜索并移除 TransferResult / TransferCallback**

```bash
grep -rn 'TransferResult\|TransferCallback' src/
```

如果这些类型不再被使用（DataServer 直接使用 DataResponseMessage），移除定义。

注意：`TransferResult` 可能在测试中被引用，检查测试文件。

- [ ] **Step 4: 构建验证**

```bash
./fly.sh build //src/...
```

Expected: 全部编译通过。

- [ ] **Step 5: Commit**

```bash
git add -A src/
git commit -m "refactor: remove obsolete on_data_request, submit_transfer, TransferCallback"
```

---

### Task 7: 全量测试验证

- [ ] **Step 1: 运行 C++ 单元测试**

```bash
./fly.sh test //src/storage/tests:data_service_test //src/storage/tests:database_test //src/agent/tests:worker_agent_test //src/agent/tests:master_agent_test //src/network/tests:data_transfer_test
```

Expected: 全部通过。如有失败，修复。

- [ ] **Step 2: 构建并安装**

```bash
./fly.sh build //src/main/cpp:fly && ./fly.sh install
```

- [ ] **Step 3: 运行 QA 测试**

```bash
python3 qa/runqa -j 4
```

Expected: 全部通过。

- [ ] **Step 4: 运行 10 轮稳定性测试**

```bash
bash /tmp/opencode/qa_10rounds.sh
```

Expected: 10/10 全部通过。

- [ ] **Step 5: Commit（如果有修复）**

```bash
git add -A
git commit -m "test: verify DataService data server stability"
git push
```
