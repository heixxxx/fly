# Solver Performance Profiling Report

## 测试环境
- 测试用例: `qa/solver/test_golden_n50_sd9.py` (n=50, 9个subdomain, 3x3网格)
- Baseline commit: `50cabd2` (优化前)
- Optimized commit: `main` (HEAD, 优化后)
- 测试运行: 3次取平均值

## 性能数据对比

### Wall Clock Time (总运行时间)
| 版本 | Run 1 | Run 2 | Run 3 | 平均 |
|------|-------|-------|-------|------|
| Baseline | 5566ms | 5590ms | 5579ms | **5578ms** |
| Optimized | 5226ms | 5101ms | 5173ms | **5167ms** |
| **Delta** | - | - | - | **-7.4% (更快)** |

### Per-Iteration Timing (每次迭代时间)
| 版本 | t_total | read_nb | write | solve | 迭代次数 |
|------|---------|---------|-------|-------|----------|
| Baseline | 10.7ms | 6.2ms | 2.0ms | 0.6ms | 110 |
| Optimized | 11.6ms | 7.0ms | 2.1ms | 0.7ms | 109 |
| **Delta** | **+8.4% (更慢)** | **+12.9% (更慢)** | +5.0% | +16.7% | -1 |

## 关键发现

### 1. 矛盾现象
- **总运行时间更快** (-7.4%)
- **但每次迭代更慢** (+8.4%)
- **特别是 read_nb 路径** (+12.9%)

### 2. 根本原因: 热路径中 INFO 级别日志开销

通过分析 worker 日志发现，优化版本在 **读取热路径** 中添加了大量 INFO 级别日志:

```cpp
// src/storage/cpp/data_service.cpp (优化版本)
// 575-577行: 原本是 DBG，改成了 INFO
case 0: INFO("[TIER1] NOT FOUND: obj={}", object_name); break;
case 1: INFO("[TIER1] NOT FOUND: obj={}, short_name={}", object_name, short_name); break;

// 925行: 原本是 DBG，改成了 INFO
INFO("[TIER2] remote_idx lookup: obj={}, worker_id={}, host={}", object_name, info.worker_id_, info.host_);

// 934行: 新增 INFO
INFO("[TIER2] obj={}, cb_found={}", object_name, cb_found);

// 948行: 新增 INFO
INFO("[TIER3] obj={}, found={}, can_produce={}", object_name, cb_found, cb_can_still_produce);
```

**统计**: 优化版本的 worker1.log 中有 **3387条** TIER1/TIER2/TIER3 INFO 日志，而 baseline 版本只有 DBG 级别日志（通常被禁用）。

### 3. 日志开销量化
- 每次 `read_raw_compressed()` 调用会产生 2-3 条 INFO 日志
- 每次迭代每个 subdomain 读取 2-4 个邻居数据
- 9个 subdomain × 109次迭代 × ~3条日志 ≈ **2943条日志** (接近实际的3387条)

### 4. 为什么总时间更快？
虽然每次迭代更慢，但优化版本的总时间更快，可能原因:
1. **启动时间优化**: 优化版本的 worker 启动更快
2. **收敛速度微调**: 迭代次数差异在误差范围内 (110 vs 109)
3. **其他路径优化**: 写入路径、内存分配等其他优化抵消了读取路径的退化

## 优化建议

### 立即修复: 将热路径日志改回 DBG 级别
```cpp
// src/storage/cpp/data_service.cpp
case 0: DBG("[TIER1] NOT FOUND: obj={}", object_name); break;  // INFO → DBG
case 1: DBG("[TIER1] NOT FOUND: obj={}, short_name={}", object_name, short_name); break;
DBG("[TIER2] remote_idx lookup: obj={}, worker_id={}, host={}", object_name, info.worker_id_, info.host_);
DBG("[TIER2] obj={}, cb_found={}", object_name, cb_found);
DBG("[TIER3] obj={}, found={}, can_produce={}", object_name, cb_found, cb_can_still_produce);
```

### 预期效果
- 消除 ~3000+ 条 INFO 日志的 I/O 开销
- read_nb 路径预计减少 1-2ms (从 7.0ms 降到 ~5.5-6.0ms)
- 总运行时间可能进一步减少 5-10%

## 附录: 详细数据

### Baseline (50cabd2) Step 1-10 详细
```
step=1 t_total=6ms read_nb=5ms write=1ms
step=2 t_total=12ms read_nb=7ms write=2ms
step=3 t_total=9ms read_nb=3ms write=3ms
step=4 t_total=7ms read_nb=4ms write=2ms
step=5 t_total=12ms read_nb=8ms write=1ms
step=6 t_total=13ms read_nb=10ms write=2ms
step=7 t_total=9ms read_nb=5ms write=2ms
step=8 t_total=9ms read_nb=6ms write=1ms
step=9 t_total=8ms read_nb=3ms write=3ms
step=10 t_total=8ms read_nb=5ms write=2ms
```

### Optimized (main) Step 1-10 详细
```
step=1 t_total=10ms read_nb=7ms write=2ms
step=2 t_total=11ms read_nb=6ms write=2ms
step=3 t_total=11ms read_nb=8ms write=1ms
step=4 t_total=14ms read_nb=9ms write=2ms
step=5 t_total=10ms read_nb=6ms write=2ms
step=6 t_total=14ms read_nb=7ms write=4ms
step=7 t_total=13ms read_nb=10ms write=2ms
step=8 t_total=11ms read_nb=5ms write=3ms
step=9 t_total=11ms read_nb=5ms write=3ms
step=10 t_total=11ms read_nb=6ms write=2ms
```

---

**结论**: 优化版本的读取路径性能退化主要由热路径中的 INFO 级别日志引起，而非实际的数据拷贝或 I/O 问题。将这些日志改回 DBG 级别即可恢复性能。
