# Fly 分布式任务框架 — Agent 工作指南

> 本文档为 Agent 提供项目概览、工作规范和关键设计约束。
> 实现细节见 `docs/*/module.md`，设计决策见 `docs/adr/` 与 `docs/DOC_CHANGELOG.md`。

---

## 1. 项目概述

**Fly** 是一个分布式任务执行框架，采用 C++ 核心 + Python 流程控制 + nanobind 桥接的架构。

| 组件 | 技术选型 |
|------|----------|
| C++ 标准 | C++20 |
| 编译器 | gcc12 |
| Python 绑定 | nanobind |
| 序列化 | bitsery (header-only, 版本化支持) |
| 构建系统 | Bazel + fly.sh |
| 测试框架 | gtest + pytest |
| 压缩库 | LZ4 / ZLIB / ZSTD |
| 格式化库 | fmt (header-only) |

架构分层：

```
┌─────────────────────────────────────────┐
│  Python 流程控制 (src/fly/)              │
├─────────────────────────────────────────┤
│  nanobind 导出层 (src/*/export/)         │
├─────────────────────────────────────────┤
│  C++ 核心模块 (src/*/cpp/)               │
│  - Agent (Master/Worker)                 │
│  - Task (调度/依赖图)                    │
│  - Network (Reactor/TCP)                 │
│  - Storage (Database)                   │
│  - Serialization (bitsery)              │
└─────────────────────────────────────────┘
```

---

## 2. 构建与测试

**必须使用 `./fly.sh` 而非裸 `bazel` 命令！** 直接使用 `bazel build` 不会刷新 `compile_commands.json`，导致 clangd 无法工作。

**Python 第三方依赖（cloudpickle/numpy/scipy/pytest）由 bazel 管理，无需手动 `pip install`。** `MODULE.bazel` 的 `pip.parse` 从 `requirements_lock.txt`（由 `requirements.in` 经 `pip-compile` 生成）拉取 wheel：`py_test` 走 bazel 沙箱 hermetic 拿包；`./fly.sh install` 把 wheel 复制到 `build/python/lib/python3.12/site-packages/`，生产 `fly` 二进制（嵌入 libpython）从这里加载，不依赖系统 site-packages。新增 Python 包：编辑 `requirements.in` → `pip-compile requirements.in` → 在用到的 `py_library`/`py_test` 加 `requirement("<pkg>")` → 生产代码还需把包加进 `tools/BUILD` 的 `fly_third_party_py`。

```bash
./fly.sh build [target...]     # 构建 + 刷新 clangd
./fly.sh test [target...]      # 测试 + 刷新 clangd
./fly.sh buildonly [target...] # 仅构建，不刷新
./fly.sh refresh               # 仅刷新 clangd
./fly.sh check                 # 构建 + 测试 + 刷新
./fly.sh install               # 创建 build/ 目录，symlink 到 bazel-bin 产物

# 单元测试
./fly.sh test //src/...

# QA 测试（需先构建）
./fly.sh build //src/main/cpp:fly
./qa/runqa -j 4 -t 40
./qa/runqa -j 4 qa/storage        # 只跑某目录
./qa/runqa qa/storage/test_x.py   # 只跑单个 case
```

### ⚠️ 跑单个 gtest 用例的陷阱（bazel 缓存 + gtest filter）

调试期常需跑单个 gtest 用例。这里有两个**极易踩的坑**，会让测试"看起来 PASSED 其实没跑到"：

**坑 1（主因）：gtest filter 语法 — 不带 suite 名会匹配 0 个测试。**
gtest filter 针对完整测试名 `TestSuite.TestName` 做通配匹配（仅支持 `*` / `?`，**非子串**）。
`--gtest_filter=Foo` **不会**匹配 `TestSuite.Foo`。必须写完整名 `TestSuite.Foo` 或带通配 `*Foo`。
查准测试名：`bazel-bin/src/.../xxx_test --gtest_list_tests`。

**坑 2（放大坑 1）：gtest 匹配 0 个测试时退出码仍是 0（[googletest#3820](https://github.com/google/googletest/issues/3820)，设计如此不修）。**
bazel 看到 exit 0 就判 PASSED，并把结果缓存。于是"改了断言却仍 PASSED"——**断言压根没执行**。

**正确的跑单个用例命令**（强制重跑 + 流式输出 + 完整 filter）：
```bash
bazel test --cache_test_results=no --test_output=streamed \
           --test_arg=--gtest_filter='MasterAgentTest.NonStreamWriteRegisterDelaysDataReady' \
           //src/agent/tests:master_agent_test
```
注意 `--test_output=streamed` 会显示 gtest 的 `Running N tests` —— **务必核对 N≥1**（N=0 即踩了坑 1）。

**最可靠的诊断手段**：直接执行二进制，完全绕过 bazel 缓存，stdout 全是自己控制的：
```bash
bazel build //src/agent/tests:master_agent_test
bazel-bin/src/agent/tests/master_agent_test --gtest_filter='*NonStreamWriteRegisterDelaysDataReady'
```

**bazel 缓存本身是健康的**：源码改动 → 二进制重编 → 内容 digest 变化 → 缓存失效。`--cache_test_results=no`（`--nocache_test_results`）能保证重跑，但**挡不住坑 2**（重跑后仍 0 匹配仍 exit 0）。看 bazel 输出的 `Executed N out of M test`：N=0 就是没跑到，与是否 cached 无关。

### QA 测试与 test 模块

QA 测试按模块分类在 `qa/<category>/` 子目录下（分类全表与运行口径见 [`qa/README.md`](qa/README.md)），使用 `src/test/py/e2e_tasks.py` 中定义的 @as_task 任务。

**QA case 脚本不需要 `sys.path.insert`** — fly 启动时已自动配好所有模块路径。获取 fly binary 路径用 `get_fly_binary()`，不要硬编码 `bazel-bin/...`。

添加新 QA case 时的工作流：

1. **评估是否需要新任务**：检查 `e2e_tasks.py` 中是否已有满足需求的任务（write_data, read_data, compute_sum, cross_db_* 等）
2. **若需新任务**：在 `e2e_tasks.py` 中添加，遵循现有命名风格（动词_名词，如 `write_data`）
3. **若需新 C++ 测试对象**：在 `src/test/cpp/test_object.h` 添加新类 + `src/test/export/test_export.cpp` 添加导出
4. **编写 QA 脚本**：在 `qa/<category>/` 目录创建新 `test_<name>.py` 文件，直接 import e2e_tasks 中的任务（无需 sys.path 操作）
5. **runqa 自动发现**：`test_*.py` 文件由 runqa 经 `os.walk` 递归发现（跳过 `.latest`/`.N` 日志残留与符号链接目录，避免重复收集），无需手动注册。runqa 也可显式指定目录或单文件：`./qa/runqa qa/storage` 或 `./qa/runqa qa/storage/test_x.py`

**test 模块不是用户可见的框架功能**，它仅为测试提供基础设施，不导出任何公共 API。

**runqa 日志行为**（定位失败时必须遵守）：
- `qa/logs/qa.log`：每次 runqa 运行全量清理重建，记录所有终端输出（含 ✓/✗ + 失败尾部 15 行 + fly.log 路径）
- 每个测试的 `fly.log`：`{test_dir}/{test_name}/fly.log`，**运行前清理历史，运行后覆盖**
- **定位失败测试**：直接读 `qa/logs/qa.log`（含失败详情和 fly.log 路径），不要重跑覆盖
- **禁止反复重跑同一个测试**——每次重跑会覆盖 fly.log，丢失上一次失败的现场

### 测试稳定性（零容忍）

**所有测试必须每次运行都通过。**

- **禁止 `sleep(Xms); assert(condition)` 模式** — 异步操作必须用 CV 等待（`wait_for()`、`wait_for_completion()`）或事件驱动轮询
- **禁止删除失败测试**
- **禁止 `time.sleep()` 作为同步手段** — 使用 `wait_for_*` 方法
- **QA 测试同样适用** — `bash qa/run_qa_tests.sh` 必须 100% 稳定通过

---

## 3. 代码规范

### C++ 类型别名

所有代码使用 `CM*` 前缀的类型别名（定义于 `common/cpp/common_types.h`）：

```cpp
#include <common/cpp/common_types.h>
CMString name;           // std::string
CMVector<int> ids;       // std::vector<int>
CMMap<K, V> dict;        // std::map<K, V>
CMUnorderedMap<K, V> h; // std::unordered_map<K, V>
```

### Include 路径

使用模块式路径，不使用相对路径：

```cpp
#include <core/cpp/config.h>       // 正确
#include "../cpp/config.h"         // 错误
```

### 命名规范

| 类型 | 命名示例 |
|------|----------|
| Bazel target | `fly_storage_cpp` |
| Python so | `_fly_storage.so` |
| 导出类型 | `EXStgDatabase` (EX+模块缩写+类型名) |
| 导出函数 | `ex_stg_create_database` (ex_模块缩写_函数名) |

### Python 包布局与 import 规范

两种运行布局（build install / bazel runfiles）的物理结构已统一，跨模块 import 一律写 `from module import symbol`（包根 re-export），**禁止** `from module.py.xxx import` 或 `from module.xxx import`。

**模块结构**（源码树与 build 布局一致）：
```
src/storage/
  __init__.py          # from storage.py import *（re-export 到包根）
  py/
    __init__.py        # from .database import *（级联到 py 包）
    database.py
    read_cache.py
```

**规则**：
1. **跨模块 import**：`from storage import Database`（包根导符号）
2. **模块内部跨文件**：用相对导入 `from .ras_graph import _load_matrix`
3. **`_` 前缀规则**：只有完全确定仅模块内部使用的函数/类/变量才用 `_` 前缀对外隐藏；否则一律不加 `_` 前缀，允许通过 `from module import *` 导出给外部使用
4. **禁止 `__all__`**：跨模块导出靠 `from .submod import *` 级联 + 默认行为（非 `_` 开头的名字自动导出），不维护 `__all__` 列表
5. **公共 API 入口**：用户可见 API 从 `from fly import xxx` 获取，`fly` 从各模块 import 再 re-export

---

## 4. 关键模块

### 存储层 (src/storage/)

| 文件 | 职责 |
|------|------|
| `database.h/cpp` | 统一存储接口，调用线程序列化+压缩，WBQ 仅落盘 |
| `data_writer.h/cpp` | 写入聚合器：compress_to_buffer（流式管线）+ write_record（磁盘写入） |
| `data_reader.h/cpp` | 数据读取，按 writer_id 索引 |
| `fly_buffer_stream.h` | FlyBufferStreamBuf（streambuf→FlyBuffer）+ CountingStreamBuf |
| `data_service.h/cpp` | 统一内存索引：local_idx + remote_idx + db_paths_ + worker_registry |
| `data_server.h/cpp` | epoll + send_thread_pool 数据服务（响应远程 Worker 数据请求） |
| `object_cache.h` | 两层 LRU 读缓存：low=压缩字节(FlyBufferPtr shared_ptr，零拷贝共享)，high=反序列化对象(std::any 持 CMSharedPtr<T>)。write_object complete_ 填 low，read_object_compressed 查/填 low，read_object<T> 查/填 high（cache="none" 时完全 bypass）；LFU 淘汰 + 30s 保护期 + 1.5× 硬限制 |
| `local_index.h/cpp` | 增量持久化索引，IdxOpType(ADD/REMOVE) 追加写入 |
| `storage_manager.h/cpp` | Database 生命周期管理，单例 |
| `py/database.py` | Python Database 类（write_object/read_object） |
| `py/__init__.py` | 导出 C++ 存储类型 |

### 网络层 (src/network/)

| 文件 | 职责 |
|------|------|
| `transport_interface.h` | Transport 抽象接口（socket 操作薄包装）+ 共享 `recv_exact`（循环 recv 直到读满）|
| `tcp_socket.h/cpp` | TCPSocketTransport — POSIX TCP 实现 |
| `epoll_multiplexer.h/cpp` | EpollMultiplexer 抽象接口 + 实现（事件复用） |
| `connection_manager.h` | ConnectionManager 抽象接口（conn_id 管理 + 事件分发）；`connect()` 失败返回 0 不抛（0=失败 sentinel，conn_id 从 1 起） |
| `tcp_connection_manager.h/cpp` | TcpConnectionManager — 基于 Transport+EpollMultiplexer |
| `reactor.h/cpp` | 单线程事件循环（持有 ConnectionManager） |
| `message_protocol.h/cpp` | MessageProtocol（通用帧协议）+ DataResponseProtocol（两段式，避免大 payload 用户态拷贝）+ 共享 `read_be32`/`write_be32`（大端 32 位整数读写，全网络层帧解析共用） |
| `message_types.h` | 消息枚举 / 消息结构定义（含 MessageHeader）。消息类型语义全表见 [docs/network/module.md](docs/network/module.md)「消息类型总表」（唯一权威口径，不在此复制） |
| `data_client_pool.h/cpp` | 并发限制的数据请求池（pool_size 限制 in-flight 请求数） |
| `net_quality_monitor.h/cpp` | per-host 网络质量评分表（RTT/带宽 EMA），供 DataService TIER2 按连接性排序远程读副本；被动 RTT 采集 + 主动带宽探测双数据源 |

### 任务系统层 (src/task/)

| 文件 | 职责 |
|------|------|
| `dependency_graph.h/cpp` | 任务依赖管理，is_data_ready() / get_task_dependencies() |
| `worker_manager.h/cpp` | Worker 状态管理，动态属性 update_capabilities() |
| `task_scheduler.h/cpp` | 基于 Worker capabilities 匹配的调度器 |
| `metadata_manager.h/cpp` | 任务元数据（仅 lifecycle） |
| `heartbeat_monitor.h/cpp` | 心跳监控 |
| `py/task.py` | @as_task() 装饰器、task_name()、任务注册 |
| `py/__init__.py` | 导出 as_task, task_name |

### Agent 层 (src/agent/)

| 文件 | 职责 |
|------|------|
| `master_agent.h/cpp` | Master 节点：失败任务持久化、写入注册依赖满足、load_db 恢复、register_worker(0) 自注册 |
| `worker_agent.h/cpp` | Worker 节点：任务执行、动态属性、on_idx_load_command() 按 writer_ids 加载 |
| `task_executor.h/cpp` | 任务执行器 |
| `pending_rpc_map.h` | PendingRpcMap 模板：worker_agent 5 套同步 RPC（DbPath/WriteRegister/Freeze/VarOp/Remove）共享的 mutex+cv+map 基础设施，消除重复样板 |
| `data_fetch.h` | fetch_from_worker free function：master/worker 共享的 DataClient 远程读取封装 |
| `py/agent.py` | Master/Worker/FlyAgent Python 类 |
| `py/executor.py` | Python 侧任务执行器 |
| `py/__init__.py` | 导出 Master, Worker, FlyAgent |

### 其他模块

| 路径 | 职责 |
|------|------|
| `src/common/cpp/common_types.h` | CM* 类型别名 |
| `src/common/cpp/writer_context.h` | WorkerAgentContext（回调模式） |
| `src/common/cpp/writer_id.h` | generate_writer_id()（8-char hex UUID） |
| `src/core/cpp/config.h/cpp` | 配置管理 |
| `src/core/py/__init__.py` | get_config() + Config 导出（合并了原 config.py） |
| `src/serialization/cpp/serialization_macros.h` | FLY_SERIALIZE, FLY_ENCODE/DECODE |
| `src/export/cpp/export_macros.h` | FLY_EXPORT_* 宏 |
| `src/log/cpp/logger.h/cpp` | DBG/INFO/WARN/ERR 日志宏，CM_FORMAT_CLASS/ENUM |
| `src/message/` | 消息日志系统（`_fly_message` 扩展 + fly.message API 簇），详见 `docs/message-system.md` |
| `src/monitor/` | cluster monitor 采集落盘 + Web GUI，详见 `docs/monitor-design.md` |
| `src/solver/` | 分布式 RAS 求解器，详见 `docs/solver/module.md` |
| `src/fly/` | Python 公共 API 顶层包（`__init__.py`/`runtime.py`/`main.py`/`project.py`/`mapreduce.py`/`userdoc.py`/`bootstrap.py`），公开符号总表见 `docs/python-api/module.md` |
| `src/test/` | 测试基础设施：TestObject（可序列化 C++ 测试对象）、e2e_tasks（QA 任务集合）、test_tasks（单元测试任务集合）。详见 `docs/test/module.md` |

---

## 5. 宏参考

Agent 禁止直接调用 bitsery/nanobind 原始 API，必须通过以下宏。

### 序列化

FlyBuffer 是统一字节缓冲区（内部存储为 CMString），兼容 bitsery adapter 和 Python pickle：
- `FLY_ENCODE_TO_BUFFER` 直接写入 FlyBuffer（零拷贝）
- `FlyBufferStreamBuf` 将 `std::streambuf` 桥接到 FlyBuffer（流式管线）
- `FlySerBuf` 是 FlyBuffer 的别名，用于 bitsery 内部

```cpp
// 声明（在 struct 内）— 自动生成 serialize() + fly_serialize() + fly_deserialize()
FLY_SERIALIZE(field1, field2);

// 编码/解码
FLY_ENCODE(struct_obj, output_string);
FLY_DECODE(input_string, StructType, output_obj);
FLY_ENCODE_TO_BUFFER(obj, output_buffer);    // → FlyBuffer
FLY_DECODE_FROM_BUFFER(buffer, StructType, output_obj);
```

### 日志

```cpp
#include <log/cpp/logger.h>
DBG("msg {}", arg);   INFO("msg {}", arg);
WARN("msg {}", arg);  ERR("msg {}", arg);

CM_FORMAT_CLASS(ns::Type, "({}, {})", v.x, v.y);  // 全局作用域
CM_FORMAT_ENUM(ns::Enum, A, B, C);                 // 输出 "A", "B", "C"
```

### Python 导出

```cpp
FLY_EXPORT_MODULE(_fly_module) { /* 导出代码 */ }
FLY_EXPORT_CLASS(ClassName, "EXModClassName")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_ATTR("attr", &Class::getter)
    FLY_EXPORT_METHOD("method", &Class::method)
    FLY_EXPORT_SERIALIZE(Class);
FLY_EXPORT_FUNCTION("ex_mod_func", [](args) { return result; });
FLY_EXPORT_ENUM(EnumType, "EXModEnumType")
    FLY_EXPORT_ENUM_VALUE("NAME", EnumType::NAME);
```

---

## 6. 设计约束

### Python 公共 API

用户通过 `from fly import ...` 使用函数级 API，无需了解 Master/Worker 内部实现：

```python
from fly import open_db, as_task, task_name, launch_workers, wait_tasks
from fly import get_agent  # 进阶：直接访问 Agent 单例
```

**导出列表**：以 [docs/python-api/module.md](docs/python-api/module.md)「公开符号总表」为唯一权威口径（数据库/任务/Worker 编队 `ensure_workers`/Project/缓存/消息/MapReduce/UserDoc/Monitor 等），此处不复制清单。

**不导出**: `Master`, `Worker`, `FlyAgent`（内部类，通过 `agent.agent` 模块可访问但不推荐用户使用）

### 内部接口（用户不应使用）

- **`launch_workers()`**: 始终使用 process 模式（子进程 Worker，独立 DataService 单例）。thread 模式已移除
- **`fly.runtime.reset()`**: 进程内 Agent 重置仅用于测试。用户场景下 Agent 生命周期由 fly 二进制管理，不允许手动 reset
- **`Master` / `Worker` 直接构造**: 用户通过 `launch_workers()` 和 `get_agent()` 间接使用，不应直接 `Master()` 构造

### Task db 归属（强制规范）

- **每个 task 函数的第一个参数必须是该 task 所属的 db 对象**——业务上不允许不同启动流程向同一 db 写入（如求解阶段 task 不得向准备矩阵阶段的 db 写入）
- 归属自动推导（`TaskSubmissionSpec.owner_db_path_`，master 从第一个 db 参数解析）；第一个 db 不在首位 → master WARN 规范偏移；例外用 `@as_task(owner=callable)` 显式覆盖
- 失败记录按归属落盘 `{owner_db_path}/failed_tasks.bin`；`fly.restart_failed_tasks(dbs)` 传 db/db_path/list 自动搜索重投（无归属 task fallback `{log_dir}`）
- restart 前相关 db 须已 load（入参路径形态自动 load 兜底）；记录内路径快照按运行时 uid 索引自愈（文件级原子：任一 db 引用无法解析 → 整 bin 保留待重试）
- data_path 是 db 级属性存 `_DB_META`（JSON version 2，2026-08-26 合并 _DB_CHAIN），task 参数编码 `__fly_db2__:{uid}:{db_path}` 不携带 data 段
- 详见 `docs/DEVELOPMENT_GUIDELINES.md` §15「Task db 归属规则」

### 数据命名与依赖

- Task inputs 必须使用 `db.get_obj_name("name")` 获取全名（`db_path:short_name`），短名无法匹配 DataService 索引
- `on_data_ready()` 是唯一数据就绪入口：更新 remote_idx + _DB_META + dependency graph + schedule_tasks()
- `write_object` 开始时即触发依赖满足（无需等异步落盘完成）
- **写入架构**：调用线程完成序列化+压缩（`compress_to_buffer` 流式管线），WBQ 后台线程仅执行 `write_record` 磁盘写入

### writer_id 解耦

- writer_id = 8-char hex UUID，Database 构造时生成
- idx 文件：`{writer_id}.idx`，data 文件：`data_{writer_id}_{index:03}.dat`
- **load_db 时 Master 不加载任何 idx 到 local_idx**，所有旧数据通过 remote_idx 经 Worker 提供
- Worker 按 hostname 分配 idx 加载任务（含 Master 的 writer_id）
- Master 仍可写新数据，通过 `register_worker(0)` 供 Worker 读取

### 动态 Worker 属性

Worker 在 Task 执行中可动态增/删/查属性，Master 实时重调度。Task 通过 `@as_task(requires=["gpu"])` 声明需求。`fail_unscheduleable_tasks=1`（默认）时，永远无法调度的 Task 立即 FAILED 并持久化。

按属性申请**现有** worker 用 `fly.ensure_workers(workers, timeout, exclude)`（幂等、两阶段收集 IDLE→BUSY、静态预检不足立即报错），与 `@as_task(requires=...)` 配对使用；契约详见 `docs/python-api/module.md` 与 `docs/issues/`009。

### 对象删除

Worker 端 `db.remove_object()` 自动通知 Master。Master 端需额外调用 `broadcast_object_removed()`。冻结后不允许删除。

### DataService 核心

进程级单例，Master/Worker 通用：local_idx（本地写入索引）、remote_idx（远程位置缓存）、db_paths_（db_id → 路径注册表）、worker_registry、transfer_server（IOThreadPool）。读取三层降级：try_read_local → lookup_remote_idx → request_remote_data。详见 `docs/storage/module.md`。

### 失败任务

Task 失败时自动序列化到 `log_dir/failed_tasks.bin`。`restart_failed_tasks(path)` 重新提交。详见 `docs/agent/module.md`。

---

## 7. Agent 工作指南

### 必须遵循

1. **使用 `./fly.sh`** 而非裸 bazel
2. **TDD 流程**：先写测试，再写实现，测试通过后提交
3. **C++20 / gcc12**
4. **模块式 include 路径**：`<module/cpp/file.h>` 格式
5. **调试必须加载 `systematic-debugging-analysis` skill**：先加日志运行观察，禁止仅靠静态分析猜测
6. **并发封装优先**：多线程共享的容器用 `ConcurrentMap`/`ConcurrentUnorderedSet`，pending 状态机（登记→等完成→消费）用 `PendingRpcMap`，禁止新增裸 mutex+容器成员对；cv notify 必须持锁。详见 `docs/DEVELOPMENT_GUIDELINES.md` §13

### 禁止事项

1. 禁止直接使用 `bazel build` 或 `bazel test`
2. 禁止使用相对路径 include
3. 禁止直接调用 bitsery/nanobind 原始 API（必须通过宏）
4. 禁止跳过测试直接提交
5. **禁止在 `qa/` 下用 `rm -rf test_*` 等通配符删除文件** —— `test_*` 通配符会同时匹配 `test_xxx.py` 源文件和 `test_xxx/` 日志目录，极易误删测试源码。qa 下的清理：
   - 未被 git 跟踪的无用文件（日志残留）：用 `git clean -fd qa/`（仅删 untracked，不碰 git 跟踪的源码）
   - 有用的非测试资源（如预生成矩阵）：放到 `qa/<dir>/matrices/` 等专用资源目录，不要散落在 test 目录下
5. 禁止无日志调试
6. **禁止归因为"之前代码就存在的问题"**：所有 crash 和不稳定问题必须视为本次代码修改引入的，不得以"pre-existing bug"为由跳过
7. **禁止忽略任何 crash 和不稳定问题**：发现的第一时间必须修复，不允许搁置或推迟
8. **禁止使用 `/tmp` 存放中间任务数据** —— 中间文件放在项目 `.work/` 目录下，任务结束前 `rm -rf .work/` 清理。`/tmp` 无限累积会填满磁盘导致 WSL2 崩溃

### 崩溃与不稳定性零容忍

- 所有 crash（SIGSEGV、SIGABRT 等）必须立即修复，不得标记为"已知问题"
- 所有间歇性失败（flaky test）必须立即修复，不得提高超时或增加重试
- 稳定性测试（50 轮以上）必须 100% 通过，任何一轮失败都是必须修复的 bug

### LSP 误报

以下 LSP 错误均为 **编译期虚拟路径误报**，`./fly.sh build` 可正常通过，忽略即可：
- `common/cpp/writer_id.h file not found` — virtual includes 路径
- `No template named 'remove_cvref_t' in namespace 'fmt'` — clangd 解析 bazel 虚拟头文件
- `Import "_fly_*" could not be resolved` — nanobind 动态生成的 .so 类型

### 新模块模板

```
src/new_module/
├── cpp/
│   ├── new_module.h      # #pragma once, 使用 CMString/CMMap
│   ├── new_module.cpp    # #include <module/cpp/new_module.h>
│   └── BUILD             # name="fly_new_module_cpp"
├── export/
│   ├── new_module_export.cpp  # FLY_EXPORT_MODULE(_fly_new_module)
│   └── BUILD             # cc_binary, name="_fly_new_module.so"
├── py/
│   ├── __init__.py
│   ├── new_module.py     # Python 侧封装（如有）
│   └── BUILD             # py_library
└── tests/
    ├── new_module_test.cpp
    ├── new_module_test.py
    └── BUILD
```

---

*文档更新日期: 2026-08-28*
