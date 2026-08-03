# DataService 锁争用 micro-benchmark — 基线数据（优化前）

> 测试日期：2026-08-03
> 测试方法：`./bazel-bin/src/storage/tests/data_service_concurrency_bench`
> 优化前状态：DataService 单一 `std::mutex mutex_` 保护所有数据域
> 数据来源：WSL2 单机，5 轮 × 500ms 取中位数

## 关键发现：单 mutex 导致并发负伸缩

**核心证据**：多线程并发读的吞吐**反而低于单线程**。这是单 mutex 串行化的典型表现——并发读被互斥锁串行化，叠加锁竞争开销，吞吐不升反降。

## 基线数据

### 场景 A：lookup_all_remote_idx 跨域读（remote_idx + worker_registry）

| 线程数 | ops/sec | 相对单线程 |
|--------|---------|-----------|
| 1 | 3516 | 1.00x |
| 2 | 1440 | **0.41x**（负伸缩） |
| 4 | 864 | **0.25x** |
| 8 | 948 | **0.27x** |

**诊断**：跨域 lookup 在单 mutex 下，8 线程并发只有单线程 27% 吞吐。锁争用主导。

### 场景 B：读写混合（1 写线程 + N 读线程）

| 读线程数 | ops/sec |
|----------|---------|
| 1 | 1812 |
| 3 | 740 |
| 7 | 892 |

**诊断**：写线程持锁更新 remote_idx，读线程全部阻塞，吞吐随读线程增加而下降。

### 场景 C：local + remote 混合并发

| local 线程 | remote 线程 | ops/sec |
|-----------|-------------|---------|
| 1 | 1 | 600800 |
| 2 | 2 | 234400 |
| 4 | 4 | 256800 |

**诊断**：local 读（has_local_object 极轻量）与 remote 读争同一把锁，互相拖累。2+2 线程只有 1+1 的 39%。

### 场景 D：纯 remote 单域读（get_remote_workers + has_remote_location）

| 线程数 | ops/sec | 相对单线程 |
|--------|---------|-----------|
| 1 | 3526 | 1.00x |
| 2 | 4020 | 1.14x |
| 4 | 930 | 0.26x |
| 8 | 958 | 0.27x |

**诊断**：即使是单域读，单 mutex 也导致 4+ 线程吞吐崩塌。

## 优化目标

分片锁（local/remote/worker/db_paths 各自 shared_mutex）+ shared_lock 读并发后，期望：
- 多线程读吞吐 ≥ 单线程（正向伸缩，消除负伸缩）
- local 读与 remote 读互不阻塞（场景 C 应接近线性伸缩）
- 跨域 lookup（场景 A）双 shared_lock 并发，吞吐显著提升

优化后数据将补充到本文档对比。
