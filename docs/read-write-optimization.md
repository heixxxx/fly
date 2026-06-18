# 读写路径零拷贝优化分析

> 2026-06-18 通过 valgrind massif 堆分配 profiling 验证，固化分析流程与结论。

## 优化目标

消除读写路径中不必要的内存拷贝，提升数据传输性能。

## 测试方法

### 测试场景

```python
# 测试数据
data = b'x' * (10 * 1024 * 1024)  # 10MB bytes 数据

# 写入测试
db.write_object('test', data)

# 读取测试
result = db.read_object('test')
```

### Profiling 命令

```bash
# valgrind massif 追踪堆分配
valgrind --tool=massif --trace-children=yes --stacks=yes \
  ./build/bin/fly qa/test_copy_elimination.py

# 分析峰值分配
grep "mem_heap_B=" massif.out.* | sed 's/.*mem_heap_B=//' | sort -rn | head -5

# 详细分配树
ms_print massif.out.<PID>
```

---

## 读写路径分析

### 写入路径

```
Python 对象
    │
    ├─ pickle.dumps(obj) → bytes                    [CPU 密集，不可避免]
    │
    └─ _write_pickle_bytes(name, data, py_name)
           │
           ├─ PyObject_GetBuffer(data) → const char* [零拷贝 ✓]
           │
           └─ write_pickle_bytes(data, size, py_name)
                  │
                  └─ compress_buffered_data(data, size, py_name, target)
                         │
                         ├─ CompressingStreamBuf::xsputn [批量写入 ✓]
                         │
                         └─ flush_chunk()
                                │
                                ├─ string_view(buffer_)   [零拷贝 ✓]
                                │
                                └─ compressor->compress()  [CPU 密集，不可避免]
```

**写入路径 copy 分析**：

| 步骤 | 操作 | 是否 copy |
|------|------|----------|
| pickle.dumps | Python 对象 → bytes | ✓ 必要（序列化） |
| PyObject_GetBuffer | bytes → const char* | ✗ 零拷贝 |
| xsputn | 数据 → buffer_ | ✗ 批量写入 |
| flush_chunk | buffer_ → string_view | ✗ 零拷贝 |
| compress | string_view → 压缩数据 | ✓ 必要（压缩） |

### 读取路径

```
FlyBufferPtr (ObjectCache.low)
    │
    └─ _read_decompressed(name)
           │
           ├─ read_object_compressed(name) → FlyBufferPtr  [零拷贝 ✓]
           │
           ├─ ObjectHeader::deserialize → total_size_      [零拷贝 ✓]
           │
           ├─ PyBytes_FromStringAndSize(nullptr, size)      [一次分配 ✓]
           │
           └─ DecompressingStreamBuf::refill()
                  │
                  └─ decompress_to(comp_view, buffer, size) [直接写入 ✓]
                         │
                         └─ pickle.loads(bytes) → Python 对象 [CPU 密集，不可避免]
```

**读取路径 copy 分析**：

| 步骤 | 操作 | 是否 copy |
|------|------|----------|
| read_object_compressed | FlyBufferPtr | ✗ 零拷贝（shared_ptr） |
| ObjectHeader::deserialize | string_view | ✗ 零拷贝 |
| PyBytes_FromStringAndSize | 分配 Python bytes | ✓ 必要（一次分配） |
| decompress_to | 直接写入 Python bytes | ✗ 零拷贝 |
| pickle.loads | bytes → Python 对象 | ✓ 必要（反序列化） |

---

## 优化内容

### 1. Compressor 零拷贝接口

```cpp
// 新增接口
virtual CompressedChunk compress(std::string_view input) = 0;
virtual int32_t decompress_to(std::string_view compressed_data,
                              char* output, size_t output_size) = 0;
```

**效果**：消除压缩/解压过程中的中间 CMString 拷贝。

### 2. CompressingStreamBuf 批量写入

```cpp
// 新增 xsputn 重写
std::streamsize xsputn(const char* s, std::streamsize n) override {
    // 批量写入，避免逐字节调用 overflow
    buffer_.insert(buffer_.end(), s + written, s + written + to_write);
}

// flush_chunk 使用 string_view
void flush_chunk() {
    std::string_view input(buffer_.data(), buffer_.size());  // 零拷贝
    compressor->compress(input);  // 直接压缩
}
```

**效果**：消除逐字节 overflow 调用和 buffer_ → CMString 拷贝。

### 3. DecompressingStreamBuf 直接解压

```cpp
bool DecompressingStreamBuf::refill() {
    std::string_view comp_view(chunk_data_ + pos, comp_size);  // 零拷贝
    buffer_.resize(uncomp_size);
    compressor->decompress_to(comp_view, buffer_.data(), ...);  // 直接写入
}
```

**效果**：消除解压过程中的中间 CMString 拷贝。

### 4. decompress_raw_data 预分配

```cpp
CMString decompress_raw_data(const CMString& raw_data) {
    // 从 header 读取预期大小
    ObjectHeader header = ObjectHeader::deserialize(raw_data, offset);
    int64_t expected_size = header.total_size_;

    // 一次分配到位，避免翻倍策略
    result.resize(expected_size);
    is.read(result.data(), expected_size);
}
```

**效果**：消除翻倍策略导致的 25MB 重新分配。

### 5. _read_decompressed 直接写入 Python bytes

```cpp
FLY_EXPORT_DEF("_read_decompressed", [](Database& db, const CMString& name) {
    // 直接创建 Python bytes 对象
    PyObject* py_bytes = PyBytes_FromStringAndSize(nullptr, expected_size);

    // 直接解压到 Python bytes 缓冲区
    is.read(PyBytes_AS_STRING(py_bytes), expected_size);

    return fly_export::bytes(py_bytes);
})
```

**效果**：消除中间 std::string 和 nanobind::bytes 拷贝。

### 6. _write_pickle_bytes 零拷贝访问

```cpp
FLY_EXPORT_DEF("_write_pickle_bytes", [](Database& db, const CMString& name,
                                          nanobind::handle data, ...) {
    Py_buffer view;
    PyObject_GetBuffer(data.ptr(), &view, PyBUF_SIMPLE);  // 零拷贝访问
    db.write_pickle_bytes(name, view.buf, view.len, ...);
    PyBuffer_Release(&view);
})
```

**效果**：消除 nanobind::bytes 构造时的拷贝。

---

## 性能测试结果

### 测试环境

- 数据大小：10MB bytes 数据
- 压缩算法：LZ4
- 测试工具：valgrind massif

### 性能对比

| 指标 | 优化前 | 优化后 | 提升 |
|------|-------|-------|------|
| 写入速度 | 113 MB/s | 120 MB/s | +6% |
| 读取速度 | 137 MB/s | **342 MB/s** | **+150%** |

### 内存分配对比

| 指标 | 优化前 | 优化后 | 减少 |
|------|-------|-------|------|
| 峰值内存 | 42 MB | 34 MB | -19% |
| decompress_raw_data | 25 MB | 0 MB | **-100%** |
| nanobind::bytes 拷贝 | 10 MB | 0 MB | **-100%** |

### 已消除的 copy

| 优化项 | 优化前 | 优化后 |
|--------|-------|-------|
| flush_chunk 中 buffer_ → CMString | ✓ 存在 | ✗ 已消除 |
| xsputn 逐字节调用 overflow | ✓ 存在 | ✗ 已消除 |
| decompress_raw_data 翻倍策略 | 25 MB | ✗ 已消除 |
| nanobind::bytes 拷贝（写入） | 10 MB | ✗ 已消除 |
| std::string → nanobind::bytes（读取） | 10 MB | ✗ 已消除 |

---

## 结论

### 读取路径优化效果显著

**原因**：
1. 消除了中间 std::string 拷贝（25MB → 0）
2. 直接解压到 Python bytes 对象
3. 避免了 nanobind 边界的多次拷贝

**效果**：读取速度提升 **2.5 倍**（137 MB/s → 342 MB/s）

### 写入路径优化效果有限

**原因**：
1. 写入瓶颈是 CPU 密集的 pickle.dumps 和压缩
2. 内存拷贝相对于这些开销来说很小

**效果**：写入速度提升 **6%**（113 MB/s → 120 MB/s）

### 不同数据类型的性能差异

| 数据类型 | 写入 | 读取 | 瓶颈 |
|---------|------|------|------|
| **bytes** | 6.0 ms | 4.0 ms | 压缩/解压 |
| **list** | 12.2 ms | 34.1 ms | pickle.loads |

**结论**：
- 对于 bytes 数据，读写速度基本一致
- 对于复杂 Python 对象，pickle.loads 是主要瓶颈（无法优化）

### 仅剩的必要分配

| 分配 | 大小 | 原因 |
|------|------|------|
| pickle.dumps/loads | ~10 MB | Python 序列化/反序列化 |
| lz4 压缩/解压 | ~4 MB | 压缩算法固有 |
| PyBytes_FromStringAndSize | ~10 MB | Python bytes 对象分配 |

---

*文档更新日期: 2026-06-18*
