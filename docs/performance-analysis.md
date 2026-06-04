# 性能分析参考

> 本文档包含性能分析工具配置和内存分配器基准测试结果。覆盖率测试相关内容已迁移至 [`coverage-testing.md`](coverage-testing.md)。

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
