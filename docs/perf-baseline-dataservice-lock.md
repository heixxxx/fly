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

---

## 优化后数据（分片 shared_mutex）— 2026-08-03

### 改造方案

单 `std::mutex mutex_` 拆为 5 把 `std::shared_mutex`（local/remote/worker/db_paths/cb）。
读方法用 `shared_lock`（并发），写方法用 `unique_lock`（独占）。
跨域读（lookup_all_remote_idx）持 remote+worker 双 shared_lock（shared_lock 互相兼容，无死锁）。
cv 改 `condition_variable_any`（配合 shared_mutex 的 unique_lock）。

### 优化前后对比（核心指标）

| 场景 | 线程数 | 优化前(单mutex) | 优化后(分片shared_mutex) | 提升 |
|------|--------|-----------------|--------------------------|------|
| A 跨域 lookup | 1 | 3516 | 3380 | 0.96x（持平） |
| A 跨域 lookup | 2 | 1440 | 6302 | **4.4x** |
| A 跨域 lookup | 4 | 864 | 11186 | **12.9x** |
| A 跨域 lookup | 8 | 948 | **15226** | **16.1x** |
| B 读写混合 | 7读+1写 | 892 | **15786** | **17.7x** |
| C local+remote | 1+1 | 600800 | 2014000 | **3.4x** |
| C local+remote | 2+2 | 234400 | 3370000 | **14.4x** |
| C local+remote | 4+4 | 256800 | **4465200** | **17.4x** |
| D 单域读 | 8 | 958 | 14674 | **15.3x** |

### 关键结论：从负伸缩到正伸缩

**优化前**：多线程并发读吞吐反而低于单线程（锁争用主导，串行化 + 竞争开销）。
**优化后**：多线程吞吐随线程数正向伸缩：
- 场景 A：1线程 3380 → 8线程 15226（4.5x 线性加速，原先 8 线程降到 948）
- 场景 C：local 读与 remote 读完全独立并发（分片锁核心收益），4+4 线程达 446 万 ops/sec

### 伸缩性曲线对比

```
场景 A (lookup_all_remote_idx 跨域读)：
ops/sec
  16000 │                                    ● 优化后(15226)
  14000 │
  12000 │                          ●(11186)
  10000 │
   8000 │
   6000 │                ●(6302)
   4000 │  ●优化前(3516) ─ ●优化后(3380)
   2000 │        ●(1440)
       0 └──────────────────────────────────
            1线程    2线程    4线程    8线程
         优化前: 负伸缩(下降)   优化后: 正伸缩(线性提升)
```

### 验证

- storage unit test 16/16（含 data_service_test 的 cv wait/lookup/temp、concurrency_bench）
- storage QA 28/28
- stability 50/50 零 crash

### 设计要点（回应用户反馈）

1. **无数据冗余**：worker_registry_ 保持地址唯一权威，不改 RemoteObjectMeta 数据结构。
   跨域读用双 shared_lock（互相兼容并发），避免后续 worker 地址变更的漏改风险。
2. **cv 兼容**：std::condition_variable 只接受 unique_lock<std::mutex>，分片后 cv 改
   std::condition_variable_any 配合 shared_mutex。wait 仅用于本地读等待写完成（非并发读热路径）。
3. **db_paths 快照**：try_read_local/try_read_local_raw 先 shared_lock<db_paths> 取 paths
   快照，再查 local_idx_。db_paths 运行期几乎不变，快照永远最新，无一致性窗口。

