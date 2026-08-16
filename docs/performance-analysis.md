# 性能分析参考

> 本文档包含性能分析工具配置和内存分配器基准测试结果。覆盖率测试相关内容已迁移至 [`coverage-testing.md`](coverage-testing.md)。

---

## 0. 存储层读写热路径瓶颈定位与修复（2026-08-03）

> 来源：分布式任务/文件系统架构性能瓶颈调研（storage/task/network 三层 + 8 份历史性能文档交叉核实）。

### 已完成的优化（不在此重复）

下列优化在历史迭代中已完成，本次调研核实其状态，避免重复工作：

| 优化 | 文档 | 效果 |
|------|------|------|
| 读写路径零拷贝 | （历史分析已删，git 历史可查） | 读取 137→342 MB/s (+150%)，写入 113→120 MB/s (+6%) |
| 数据面 wire 传输零拷贝 | （历史分析已删，git 历史可查） | DataResponseProtocol 两段式 + writev + FlyBufferPtr 共享，raw 全程零拷贝 |
| 热路径 INFO→DBG | （旧 profiling 快照反映旧 commit，当前已修复） | 消除读取热路径 ~3000 条/轮 INFO 日志开销 |

### 本次修复的瓶颈（S5，详见 [`roadmap.md`](roadmap.md) §S5）

经源码逐行核实，识别出 3 个真实存在、未被历史优化覆盖、零架构改动的局部实现瓶颈：

**S5-1 LocalIndex 追加流复用（写入热路径 syscall 放大）**

- **瓶颈**：`local_index.cpp` 的 `append_add`/`append_remove`/`append_marker` 每次都 `std::ofstream ofs(idx_path_, app)` 重开文件。调用链 `write_object → commit_write → WriteBackQueue execute → write_record + flush → index_->save() → append_add`，每个对象写入触发 1 次 open/write/close syscall 组，批量写 N 个小对象 = N 次重开。
- **修复**：LocalIndex 持有持久 `idx_append_stream_` 成员复用，惰性打开。`save()` 末尾显式 `flush` 保 WAL 持久化语义（原实现靠独立 ofstream 析构 flush，复用流后必须显式 flush）。`compact()`/`save_legacy()` 的 truncate/rename 路径经 `reset_append_stream()` 重置旧 fd。
- **影响面**：仅写入热路径，读路径与并发模型不变。写序仍由 WriteBackQueue 单线程保证。

**S5-2 DataReader 冷读路径消除冗余 idx 全量解析（冷读延迟放大）**

- **瓶颈**：`data_service.cpp:do_read_raw_entries`（TIER1 ObjectCache low tier miss 时进入）每次 `new DataReader`，构造函数 `index_->load()` 全量打开并解析整个 `.idx` 文件构建 entries_ map。但调用方传入的 entry 已来自 `local_idx_` 内存索引，`read_raw_bytes(IndexEntry&)` 只用 db_path/data_path 定位文件，DataReader 的 LocalIndex entries_ 完全没消费。
- **修复**：新增静态 `DataReader::read_raw_from_entry(entry, db_path, data_path)`，基于已知 entry + 路径直接定位文件 + 区间读取，不构造 DataReader、不 load idx。`find_file_path`/`read_from_file` 重构为静态核心消除重复。
- **影响面**：仅 ObjectCache miss 的冷读路径（首次读 / 缓存外的对象）。命中缓存的热对象走 `get_low` 直接返回，不受影响。

**S5-3 Database 死成员 reader_ 清理（消除无用 idx load + 常驻内存）**

- **瓶颈**：`Database::reader_`（`CMUniquePtr<DataReader>`）在构造时创建，触发一次 idx 全量 load 并持有 LocalIndex（entries_ map）内存到析构，但全树 grep 确认其方法（`read_raw_bytes`/`exists`/`find_entry`/`find_all_entries`）零调用 —— 纯死成员。每个 Database 构造白白付出一次 idx 解析 + 常驻一份 LocalIndex 内存。
- **修复**：删除 reader_ 成员及构造。DataReader 类保留（S5-2 静态方法 + data_reader_test 仍守护其 API）。

### 架构级瓶颈（部分已优化，详见 §0B）

下列触及并发模型/协议/架构，其中两项经数据验证已优化（S7），其余仍排除：

- **Reactor 单线程同步模型**（`HandlerThreadPool` 死代码未接线）—— （旧架构审查记录，2026-08-16 已删；结论：接线需 handler 线程安全全面审计，仍排除）
- ~~**`schedule_mutex_` 全局串行化**~~ —— 调度吞吐天花板。**S7-2 已部分优化**：locality 预计算移出锁外缩短持锁时间（详见 §0B）。attr-tick 条件触发方案经实测破坏 attr timeout 语义已放弃
- **WriteBackQueue 单 worker 线程** —— 写入吞吐硬瓶颈，改多线程需处理 idx 文件并发与写序（仍排除）
- ~~**DataClientPool 短连接 + 默认 4 并发**~~ —— 连接建立开销已消除（keep-alive 连接池，commit a408523）；并发上限 `pool_size` 仍在，roadmap F4 流控已降级（仍排除）
- ~~**DataService 单 mutex**~~ —— （旧架构审查待办项）。**S7-1 已优化**：分片 shared_mutex，并发读从负伸缩转为正伸缩，8线程提升 16x（详见 §0B）
- **S1-3/S3**（remote_idx/provenance 无上限累积）—— roadmap 标注待对象量真实过百万时启动（仍排除）

### 验证

storage unit test 16/16、全量 cpp unit test、全量 QA 139/139、stability 50/50 零 crash。

---

## 0B. DataService 锁分片 + schedule 锁范围优化（S7，2026-08-03）

> 数据驱动优化：先建并发 benchmark 跑出优化前基线（揭示负伸缩），优化后跑对比数据验证提升量级。详见 [`perf-baselines.md`](perf-baselines.md)。

### S7-1：DataService 分片 shared_mutex（消除并发读负伸缩）

**瓶颈定位**（benchmark 基线）：单 `std::mutex mutex_` 保护所有数据域，多线程并发读被串行化 + 锁竞争，导致吞吐随线程数下降（负伸缩）：
- 场景 A（跨域 lookup）8 线程仅单线程 27%（948 vs 3516 ops/sec）
- 场景 C（local+remote 混合）2+2 线程仅 1+1 的 39%

**方案**：拆为 5 把 `std::shared_mutex`（local/remote/worker/db_paths/cb）。读 `shared_lock`（并发）、写 `unique_lock`（独占）。跨域读（lookup_all_remote_idx）持 remote+worker 双 shared_lock（互相兼容，无死锁）。cv 改 `condition_variable_any` 配合 shared_mutex。**无数据冗余**（worker_registry 保持地址唯一权威）。

**优化前后对比**（ops/sec，5轮×500ms 取中位数）：

| 场景 | 线程数 | 优化前 | 优化后 | 提升 |
|------|--------|--------|--------|------|
| A 跨域 lookup | 8 | 948 | 15226 | **16.1x** |
| B 读写混合 | 7读+1写 | 892 | 15786 | **17.7x** |
| C local+remote | 4+4 | 256800 | 4465200 | **17.4x** |
| D 单域读 | 8 | 958 | 14674 | **15.3x** |

**核心收益**：从负伸缩转为正伸缩——8线程吞吐达单线程 4.5x（场景 A），local 读与 remote 读完全独立并发（场景 C）。

### S7-2：locality 预计算移出 schedule_mutex_ 锁外

**瓶颈**：schedule_tasks 在持 schedule_mutex_ 下查 DataService 预计算 locality hint，DataService 查询自带锁，不依赖 schedule_mutex_。

**方案**：锁外取 ready 快照 + 算 hint，再持锁注入 graph + schedule_all_available。缩短持锁时间（reactor 与 attr-tick 竞争窗口）。

**放弃的方案**：attr-tick 条件触发（仅限时 task 才触发 schedule_tasks）。实测破坏 attr timeout 语义——attr timeout 的 task 在 ready_tasks_（等匹配 worker）而非 pending_tasks_，按 pending 判断漏掉降级场景导致 task 卡死。attr-tick 无条件触发是语义必需（推进降级 + 死锁检测），不可加条件。

### 验证

storage unit test 16/16（含新增 concurrency_bench）、master_agent + task 单测、全量 QA 139/139、stability 50/50 零 crash。

---

## 1. 内存分配器对比测试

### 1.1 测试方法

```bash
# 使用 LD_PRELOAD 切换分配器，运行 QA 全量测试
LD_PRELOAD=/usr/lib/x86_64-linux-gnu/libXXX.so /usr/bin/time -v bash qa/run_qa_tests.sh
```

每个分配器运行 3 轮取平均。

### 1.2 测试结果

| 分配器 | Wall Time (avg) | Peak RSS (avg) | 页错误 (minor) | 状态 |
|--------|-----------------|----------------|----------------|------|
| **glibc** (baseline) | **17.23s** | **49.3 MB** | ~830K | 稳定 |
| **tcmalloc** | **17.53s** (+1.7%) | **55.8 MB** (+13.2%) | ~838K | 稳定 |
| **jemalloc** | **17.33s** (+0.6%) | **52.1 MB** (+5.7%) | ~418K | 稳定 |

### 1.3 分析

**结论：在当前 QA 工作负载下，替换分配器无明显性能提升。**

原因：
1. **QA 测试非分配密集型**：主要瓶颈在进程启动、网络通信、磁盘 I/O，分配开销占比低
2. **小规模测试**：QA 测试总耗时 ~17s，分配器优势在长时间运行的大规模负载中才显现
3. **Python 进程模型**：Worker 是独立子进程，C++ 侧内存分配量相对有限

**建议**：
- **当前阶段**：保持 glibc 默认分配器
- **未来扩展**：当单进程任务数 >1000 或长时间运行 Worker 场景，可重新评估 jemalloc（RSS 更优）或 tcmalloc（多线程优化）
- **不推荐 mimalloc/rpmalloc/snmalloc**：与 Python/nanobind 兼容性存在风险

### 1.4 可用分配器安装

```bash
sudo apt install libgoogle-perftools-dev  # tcmalloc
sudo apt install libjemalloc2             # jemalloc

# 使用方式（无需重编译）
LD_PRELOAD=/usr/lib/x86_64-linux-gnu/libtcmalloc.so.4 ./build/bin/fly
LD_PRELOAD=/usr/lib/x86_64-linux-gnu/libjemalloc.so.2 ./build/bin/fly
```

---

## 2. Profiling 工具配置

### 2.1 perf (CPU profiling)

```bash
# 构建 debug + opt 二进制
bazel build -c opt --copt=-g //src/main/cpp:fly

# CPU profiling
perf record -g -F 99 ./build/bin/fly [args]
perf report

# Flame graph
perf script | stackcollapse-perf.pl | flamegraph.pl > flamegraph.svg
```

### 2.2 valgrind (内存分析)

```bash
# 内存泄漏检测
valgrind --leak-check=full ./build/bin/fly [args]

# 堆分析
valgrind --tool=massif ./build/bin/fly [args]
ms_print massif.out.*

# 缓存分析
valgrind --tool=cachegrind ./build/bin/fly [args]
```

### 2.3 tcmalloc profiler

```bash
# 构建时链接 tcmalloc profiler
bazel build -c opt --linkopt=-lprofiler //src/main/cpp:fly

# CPU profiling
CPUPROFILE=/tmp/cpu.prof ./build/bin/fly [args]
pprof --text ./build/bin/fly /tmp/cpu.prof

# 堆 profiling
HEAPPROFILE=/tmp/heap ./build/bin/fly [args]
pprof --text ./build/bin/fly /tmp/heap.0001.heap
```
