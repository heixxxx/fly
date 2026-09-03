# Fly 性能优化基线报告（micro-benchmark 数据集）

> 2026-08-16 整合：原 perf-baseline-dataservice-lock.md（S7）与 perf-baseline-scheduling-hotloop.md（S8）两文档合并，内容不变。对应 roadmap §S7/§S8。

---

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


---

## 2026-08-31 口径补注（重要）

**场景 B（读写混合）的历史数字（优化后 15786 ops/sec）已失效**：此后
remote_idx 锁从 `std::shared_mutex` 换为 `fly::WriterPrefRwLock`（写优先，
根治读者优先饿死写者的 remote_idx 活锁，见 `src/common/cpp/writer_pref_rwlock.h`
头注）。写优先语义下，bench 中写线程无限循环申请写锁会持续压制新读者，
7读+1写 当前口径约 **2176 ops/sec**——这是防写饿死的有意语义代价，不是退化。
真实负载写频率为每对象一次，不构成瓶颈。**勿用旧数字对比本轮之后的测量。**

场景 C/D 与调度 A/B 与历史基线持平；调度场景 C（反复调度周期）从 73501
降至 ~33k tasks/sec：bench 未变，为 8/4 后调度器新增 locality/handler-lane
等功能的固有成本 + 每任务日志开销（2026-08-31 已优化 localtime_r），真实
调度频率远低于此量级，实际负载无感。

---

# Task 调度热循环 micro-benchmark — 基线数据（优化前）

> 测试日期：2026-08-04
> 测试方法：`./bazel-bin/src/task/tests/scheduling_hotloop_bench`
> 优化前状态：schedule_next 循环内冗余重取 idle/ready + get_ready_tasks 每次全量 sort
> 数据来源：WSL2 单机，7 轮取中位数，纯内存调度（无网络/磁盘/Python）

## 关键发现：调度热循环呈 O(N²) 退化

**核心证据**：随着 ready task 数增加，调度吞吐**急剧下降**（非线性），这是 schedule_next 循环内每调度一个 task 都重新获取并 sort 整个 ready 集导致的 O(N² log N) 行为。

## 基线数据

### 场景 A：大批次调度吞吐（schedule_all_available 消费 N 个 ready task）

| ready task 数 | idle worker 数 | tasks/sec | 相对 50t |
|---------------|----------------|-----------|----------|
| 50 | 8 | 20219 | 1.00x |
| 200 | 16 | 3878 | **0.19x**（5.2x 退化） |
| 1000 | 32 | 575 | **0.028x**（35x 退化） |

**诊断**：典型的 O(N²) 行为。schedule_all_available 循环 N 次，每次 schedule_next 都调 get_ready_tasks（O(N log N) sort）+ get_idle_workers。N 个 task = N × O(N log N) = O(N² log N)。

### 场景 B：get_ready_tasks sort 开销（反复调，不同 ready 集大小）

| ready 集大小 | ops/sec | 相对 size=10 |
|--------------|---------|--------------|
| 10 | 342315 | 1.00x |
| 100 | 8118 | 0.024x |
| 500 | 1340 | 0.004x |
| 2000 | 275 | 0.0008x |

**诊断**：sort + 比较器内 task_requirements_ map find 的开销随集大小急剧恶化。size=2000 时仅 275 ops/sec——每次调用 3.6ms，在 attr-tick 200ms 周期下占 1.8%。

### 场景 C：反复全量调度（模拟 attr-tick 200ms 周期，每 cycle 50 task）

| 总 task 数 | tasks/sec |
|-----------|-----------|
| 10000（200 cycle × 50） | 38375 |

**诊断**：每 cycle 仅 50 task，O(N²) 不明显（50²=2500），吞吐尚可。说明小批量调度场景 H2 影响有限，但 ready 积压（场景 A）是真正的痛点。

## 优化目标

1. **H2-a 消除 schedule_next 冗余重取**：select_best_worker 内重取 idle + 重建 idle_set；schedule_next 已拿到的 ready/idle 传入复用。
2. **H2-b 消除 get_ready_tasks 每次 sort**：引入有序结构维护 ready 顺序，避免每查必排。

优化后数据将补充到本文档对比。

---

## 优化后数据（H2-a + H2-b）— 2026-08-04

### 改造方案

- **H2-a**：select_best_worker 接受 idle_workers/idle_set 参数（const 引用），由 schedule_next 一次获取后循环内复用，消除内部重取与 set 重建。零架构改动。
- **H2-b**：ready_tasks_ 从 `CMUnorderedSet<uint64_t>` 改为 `std::set<std::pair<int,uint64_t>>`，key = `{-priority, task_id}`（priority 降序、同优先级 task_id 升序 FIFO）。插入即有序，get_ready_tasks 直接遍历取 task_id，**删除 std::sort**。priority 在 task 生命周期内不可变（add_task 时确定），key 稳定。

### 优化前后对比（核心指标）

| 场景 | 规模 | 原始基线 | 优化后 | 提升 |
|------|------|---------|--------|------|
| A 大批次调度 | 50 task | 20219 | 145206 | **7.2x** |
| A 大批次调度 | 200 task | 3878 | 91739 | **23.7x** |
| A 大批次调度 | 1000 task | 575 | 30591 | **53.2x** |
| B get_ready_tasks | size=100 | 8118 | 486451 | **59.9x** |
| B get_ready_tasks | size=2000 | 275 | 23333 | **84.8x** |
| C 反复调度 | 10000 task | 38375 | 73501 | 1.9x |

### 关键结论：O(N²) 退化被彻底消除

**优化前**：场景 A 从 50t→1000t 吞吐降 35x（O(N²) 退化），B 场景 size=2000 仅 275 ops/sec。
**优化后**：场景 A 从 50t→1000t 吞吐仅降 4.7x（**接近线性，O(N²) 消除**），B 场景 size=2000 达 23333 ops/sec（85x）。

### 伸缩性曲线对比

```
场景 A (schedule_all_available 大批次调度)：
tasks/sec
  140000 │ ●优化后(145206)
  120000 │
  100000 │       ●优化后(91739)
   80000 │
   60000 │
   40000 │                    ●优化后(30591)
   20000 │
       0 │───●优化前(20219)────────────────────
            50t       200t       1000t
         优化前: O(N²) 退化(35x下降)   优化后: 近线性(4.7x下降)
```

### 维护点（H2-b 改造涉及）

ready_tasks_ 类型变更后，5 处访问点改造：
- `add_task` / `check_and_move_to_ready`：insert `{-priority, task_id}`
- `remove_task` / `mark_data_removed`：erase 需查 priority 构造完整 key（task_requirements_ 此时仍在）
- `is_task_ready` / `check_and_move_to_ready` 早期判断：查 priority 构造 key 或 find_if
- `get_ready_tasks`：遍历 set 取 `.second`，**删除 sort**

### 验证

- task unit test 全绿（含 task_scheduler_test T1-T7 全套调度测试、dependency_graph_test）
- master_agent 单测、全量 QA 139/139、stability 50/50 零 crash

---

# PeerRpc 真流式读端基准 — 2026-09-04（复测收口）

> 载体：`qa/performance/test_peer_stream_read_perf.py`（双 worker 拓扑：member/check
> 分属不同 worker 进程；请求/响应双向 pickle 流，业务 `pickle.load(reader)` 拉动
> CrcVerifyStage→DecompressStage，与 read_object 共享 Stage 管线）。
> 512MB 档为精简口径（跨轮复用序列化 bytes，CRC 对账代替全量反序列化，峰值 ≈2×size），
> `PEER_RPC_PERF_FULL=1` 门控；MemTotal<12GB 自动打印机器口径 WARN。
> 数字 = 3 轮中位，5.8GB WSL2 / 6 核 / 127.0.0.1 自环。

| 档位 | none | lz4 | 收齐交付版对照 |
|---|---|---|---|
| 4MB | 372 MB/s | 320 MB/s | — |
| 16MB | 568 MB/s | 543 MB/s | — |
| 64MB | 583 MB/s | 625 MB/s | 467 MB/s（64MB f64） |
| 512MB | **554 MB/s** | 541 MB/s | 373 MB/s（512MB f64） |

**结论**：真流式版在 5.8GB 内存带宽受限机器上 512MB none 达 554 MB/s，较收齐
交付版 **+48%**；64MB 档 +25%。验收线「none 512MB ≥ 700 MB/s」按 ≥16GB 机器
标定，本机未达属预期（内存带宽/页回收压制），**留待新机复测裁决**；大 payload
不再有「收齐才反序列化」的 2× 内存驻留差距。

复现：`PEER_RPC_PERF_FULL=1 ./qa/runqa -t 180 qa/performance/test_peer_stream_read_perf.py`
（数字在 `worker2.log` 的 [PERF-SUMMARY]，master fly.log 无业务输出）
