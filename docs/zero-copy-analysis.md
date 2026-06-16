# 零拷贝数据传输分析

> 2026-06-17 通过 valgrind massif 堆分配 profiling 验证，固化分析流程与结论。

## 分析流程（可复现）

### 测试场景

```
Master 启动 2 worker：
  worker1 (gpu): task1 写 10MB temp 对象（pickle list of 1.25M ints）
  worker2 (cpu): task2 依赖 task1 输出，远程 read_object（触发 wire 传输）
```

测试文件：`qa/test_large_transfer_profile.py`
Task 定义：`src/test/py/e2e_tasks.py`（gpu_write_large_temp / cpu_read_large_remote）

### Profiling 命令

```bash
# valgrind massif 追踪 master + worker 子进程的堆分配
valgrind --tool=massif --trace-children=yes --stacks=yes \
  ./build/bin/fly qa/test_large_transfer_profile.py

# 分析各进程峰值分配
for f in massif.out.*; do
  peak=$(grep "mem_heap_B=" "$f" | sed 's/.*mem_heap_B=//' | sort -rn | head -1)
  echo "$f: peak = $((peak/1024/1024))MB"
done

# 详细分配树（找特定函数）
ms_print massif.out.<PID>
```

### 验证标准

wire 路径零用户态 copy 的判据：**massif 分配树中不出现以下函数的 heap 分配**：
- `DataResponseProtocol::encode`（CMString 拼帧）
- `MessageProtocol::encode/decode`（bitsery 序列化大字段）
- FlyBuffer→CMString 的临时 copy（`CMString(ptr, size)`）
- `substr` / `take` / `full_buf memcpy`

## 结论

### wire 路径零用户态 copy ✓

massif 分配树中：
- worker1（sender）峰值分配全在 `compress_pickle_bytes`（写入压缩）—— **无 wire egress copy**
- worker2（reader）峰值分配在 `decompress_raw_data`（解压）—— **无 wire ingress copy**
- `DataResponseProtocol` / `MessageProtocol` / `FlyBuffer→CMString` / `substr` / `take` **均不出现**

### 确认的必然分配（非意外 copy）

| 路径 | 分配 | 大小 | 性质 |
|------|------|------|------|
| pickle.dumps | list → bytes | ~10MB | 序列化，必然 |
| compress_pickle_bytes | lz4 压缩 | 4MB chunks | 压缩，必然 |
| wire send (DataServer) | — | **0** | 零拷贝（引用 FlyBufferPtr）✓ |
| wire recv (DataClient) | FlyBuffer resize | 1 次分配 | 接收缓冲，必然 |
| decompress_raw_data | bytes → 解压数据 | ~10MB | 解压，必然 |
| Python list 重建 | bytes → list | ~10MB | 反序列化，必然 |

### 已消除的 copy（本次重构成果）

| copy 点 | 重构前 | 重构后 |
|---------|--------|--------|
| write → ObjectCache put_low | FlyBuffer→CMString copy | FlyBufferPtr 共享（0 copy）|
| read → ObjectCache get_low → 返回 | any_cast CMString copy | FlyBufferPtr 共享（0 copy）|
| ObjectHeader::deserialize | 全量 CMString 临时副本 | string_view 引用（0 copy）|
| DataServer wire egress (assign) | FlyBuffer→CMString | 直接引用 FlyBufferPtr（0 copy）|
| DataServer FLY_ENCODE compressed_data_ | bitsery 序列化 100MB | 不经 bitsery（0 copy）|
| DataServer frame std::copy | payload→frame | 小字段独立编帧（0 copy）|
| DataClient wire ingress memcpy 重组 | full_buf 100MB | 分步 recv（0 copy）|
| DataClient substr | 100MB | 不 substr（0 copy）|
| DataClient take | 100MB | 直接 recv 进 FlyBuffer（0 copy）|
| DataClient FLY_DECODE compressed_data_ | bitsery 反序列化 100MB | 不经 bitsery（0 copy）|

### 仅剩的 copy（不可消除）

- **内核态 send/recv**（syscall：用户态 → socket buffer）：2 次，TCP 固有
- **pickle 序列化/反序列化**：Python 对象 ↔ bytes，序列化固有
- **lz4 压缩/解压**：必然分配
