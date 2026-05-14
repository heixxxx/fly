# Compression Layer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a compression layer between serialization and file I/O in the Fly storage module, supporting multiple algorithms (lz4 default, zlib, zstd none) with streaming/chunked compression for large objects and configurable thresholds via Config.

**Architecture:** Abstract `Compressor` base class with `compress()`/`decompress()` for whole-object compression and `compress_chunk()`/`decompress_chunk()` for streaming block compression. DataWriter compresses data before writing; DataReader decompresses after reading. IndexEntry gains a `compression_type` field. The compressed format for each stored chunk is `[int32_t uncompressed_size][int32_t compressed_size][compressed_bytes]`, enabling the reader to decompress correctly regardless of algorithm.

**Tech Stack:** C++20, Bazel, lz4 (system), zlib (system), zstd (system), zpp_bits (existing)

---

## File Structure

| File | Purpose |
|---|---|
| `src/storage/cpp/compressor.h` | Abstract `Compressor` base class, `CompressionType` enum, `CompressedChunk` struct, `CompressorFactory` |
| `src/storage/cpp/lz4_compressor.h` / `.cpp` | LZ4 compressor implementation |
| `src/storage/cpp/zlib_compressor.h` / `.cpp` | Zlib compressor implementation |
| `src/storage/cpp/zstd_compressor.h` / `.cpp` | Zstd compressor implementation |
| `src/storage/cpp/compression_utils.h` / `.cpp` | Utility functions: `compress_data()`, `decompress_data()`, write/read compressed chunk header |
| `src/storage/cpp/index_entry.h` | Add `compression_type` field (int8_t) |
| `src/storage/cpp/data_writer.h` / `.cpp` | Modify to compress before writing |
| `src/storage/cpp/data_reader.h` / `.cpp` | Modify to decompress after reading |
| `src/storage/cpp/local_index.h` / `.cpp` | Optional: compress index data on save |
| `src/core/cpp/config.h` / `.cpp` | Add compression config defaults |
| `third_party/lz4.BUILD` | Bazel BUILD for lz4 system library |
| `third_party/zlib.BUILD` | Bazel BUILD for zlib system library |
| `third_party/zstd.BUILD` | Bazel BUILD for zstd system library |
| `src/storage/tests/compressor_test.cpp` | Unit tests for all compressors |

---

### Task 1: Bazel Dependencies for LZ4, Zlib, Zstd

**Files:**
- Create: `third_party/lz4.BUILD`
- Create: `third_party/zlib.BUILD`
- Create: `third_party/zstd.BUILD`
- Modify: `WORKSPACE`

- [ ] **Step 1: Create lz4.BUILD**

```python
package(default_visibility = ["//visibility:public"])

cc_library(
    name = "lz4",
    hdrs = [
        "lz4.h",
        "lz4frame.h",
        "lz4hc.h",
    ],
    srcs = glob(["*.c"], exclude = ["**/*.o"]),
    includes = ["."],
    linkopts = ["-llz4"],
    copts = ["-std=c99"],
)
```

Wait — system libraries need `new_local_repository`. Let me use that pattern instead.

- [ ] **Step 1: Add system library repos to WORKSPACE**

Append to `WORKSPACE`:

```python
# LZ4 - fast compression library
new_local_repository(
    name = "lz4",
    path = "/usr",
    build_file = "@//third_party:lz4.BUILD",
)

# Zlib - compression library
new_local_repository(
    name = "zlib",
    path = "/usr",
    build_file = "@//third_party:zlib.BUILD",
)

# Zstd - high-performance compression
new_local_repository(
    name = "zstd",
    path = "/usr",
    build_file = "@//third_party:zstd.BUILD",
)
```

- [ ] **Step 2: Create lz4.BUILD**

```python
package(default_visibility = ["//visibility:public"])

cc_library(
    name = "lz4",
    hdrs = ["include/lz4.h", "include/lz4frame.h", "include/lz4hc.h"],
    includes = ["include"],
    srcs = [],
    linkopts = ["-llz4"],
)
```

- [ ] **Step 3: Create zlib.BUILD**

```python
package(default_visibility = ["//visibility:public"])

cc_library(
    name = "zlib",
    hdrs = ["include/zlib.h"],
    includes = ["include"],
    srcs = [],
    linkopts = ["-lz"],
)
```

- [ ] **Step 4: Create zstd.BUILD**

```python
package(default_visibility = ["//visibility:public"])

cc_library(
    name = "zstd",
    hdrs = ["include/zstd.h", "include/zstd_errors.h"],
    includes = ["include"],
    srcs = [],
    linkopts = ["-lzstd"],
)
```

- [ ] **Step 5: Verify build resolves**

Run: `cd /root/fly && bazel query '//third_party:all' 2>&1 | head -20`

Expected: Should list the targets without error. If `new_local_repository` path issues, adjust.

---

### Task 2: CompressionType Enum and Compressor Interface

**Files:**
- Create: `src/storage/cpp/compressor.h`
- Create: `src/storage/tests/compressor_test.cpp` (initial skeleton)

- [ ] **Step 1: Create `compressor.h` with CompressionType, CompressedChunk, Compressor base class, CompressorFactory**

```cpp
#pragma once

#include <common/cpp/common_types.h>
#include <cstdint>
#include <memory>
#include <stdexcept>

// Compression algorithm types
enum class CompressionType : int8_t {
    NONE = 0,
    LZ4 = 1,
    ZLIB = 2,
    ZSTD = 3,
};

// Header prefix for each compressed chunk in the storage format:
// [int32_t uncompressed_size][int32_t compressed_size][compressed_bytes...]
// This allows the reader to know the exact buffer sizes before decompression.
struct CompressedChunk {
    int32_t uncompressed_size = 0;
    int32_t compressed_size = 0;
    CMString data;  // compressed bytes
};

// Abstract compressor interface
class Compressor {
public:
    virtual ~Compressor() = default;

    // Compress an entire buffer at once
    // Returns compressed data with header
    virtual CompressedChunk compress(const CMString& input) = 0;

    // Decompress an entire buffer from a CompressedChunk
    // Expects the chunk header to already be parsed
    virtual CMString decompress(int32_t uncompressed_size, const CMString& compressed_data) = 0;

    // Compress a chunk for streaming (same as compress for most algorithms)
    virtual CompressedChunk compress_chunk(const CMString& input) = 0;

    // Decompress a chunk for streaming
    virtual CMString decompress_chunk(int32_t uncompressed_size, const CMString& compressed_data) = 0;

    virtual CompressionType type() const = 0;
    virtual CMString name() const = 0;
};

// Factory to create compressors by type
class CompressorFactory {
public:
    static std::unique_ptr<Compressor> create(CompressionType type);
    static std::unique_ptr<Compressor> create_from_name(const CMString& name);

    static CompressionType type_from_name(const CMString& name);
    static CMString name_from_type(CompressionType type);
};
```

- [ ] **Step 2: Create test skeleton**

Create `src/storage/tests/compressor_test.cpp`:

```cpp
#include <gtest/gtest.h>
#include <storage/cpp/compressor.h>

TEST(CompressionTypeTest, FromName) {
    EXPECT_EQ(CompressorFactory::type_from_name("none"), CompressionType::NONE);
    EXPECT_EQ(CompressorFactory::type_from_name("lz4"), CompressionType::LZ4);
    EXPECT_EQ(CompressorFactory::type_from_name("zlib"), CompressionType::ZLIB);
    EXPECT_EQ(CompressorFactory::type_from_name("zstd"), CompressionType::ZSTD);
}

TEST(CompressionTypeTest, NameFromType) {
    EXPECT_EQ(CompressorFactory::name_from_type(CompressionType::NONE), "none");
    EXPECT_EQ(CompressorFactory::name_from_type(CompressionType::LZ4), "lz4");
    EXPECT_EQ(CompressorFactory::name_from_type(CompressionType::ZLIB), "zlib");
    EXPECT_EQ(CompressorFactory::name_from_type(CompressionType::ZSTD), "zstd");
}

TEST(CompressorFactoryTest, CreateByType) {
    auto lz4 = CompressorFactory::create(CompressionType::LZ4);
    ASSERT_NE(lz4, nullptr);
    EXPECT_EQ(lz4->type(), CompressionType::LZ4);
    EXPECT_EQ(lz4->name(), "lz4");

    auto zlib_comp = CompressorFactory::create(CompressionType::ZLIB);
    ASSERT_NE(zlib_comp, nullptr);
    EXPECT_EQ(zlib_comp->type(), CompressionType::ZLIB);

    auto zstd_comp = CompressorFactory::create(CompressionType::ZSTD);
    ASSERT_NE(zstd_comp, nullptr);
    EXPECT_EQ(zstd_comp->type(), CompressionType::ZSTD);

    auto none_comp = CompressorFactory::create(CompressionType::NONE);
    ASSERT_NE(none_comp, nullptr);
    EXPECT_EQ(none_comp->type(), CompressionType::NONE);
}

TEST(CompressorFactoryTest, CreateByName) {
    auto lz4 = CompressorFactory::create_from_name("lz4");
    ASSERT_NE(lz4, nullptr);
    EXPECT_EQ(lz4->type(), CompressionType::LZ4);

    auto none_comp = CompressorFactory::create_from_name("none");
    ASSERT_NE(none_comp, nullptr);
    EXPECT_EQ(none_comp->type(), CompressionType::NONE);
}
```

- [ ] **Step 3: Create `compressor.cpp` with CompressorFactory (NONE only initially)**

Create `src/storage/cpp/compressor.cpp`:

```cpp
#include <storage/cpp/compressor.h>

// NoneCompressor - pass-through, no compression
class NoneCompressor : public Compressor {
public:
    CompressedChunk compress(const CMString& input) override {
        CompressedChunk chunk;
        chunk.uncompressed_size = static_cast<int32_t>(input.size());
        chunk.data = input;  // no compression
        chunk.compressed_size = static_cast<int32_t>(chunk.data.size());
        return chunk;
    }

    CMString decompress(int32_t uncompressed_size, const CMString& compressed_data) override {
        (void)uncompressed_size;
        return compressed_data;
    }

    CompressedChunk compress_chunk(const CMString& input) override {
        return compress(input);
    }

    CMString decompress_chunk(int32_t uncompressed_size, const CMString& compressed_data) override {
        return decompress(uncompressed_size, compressed_data);
    }

    CompressionType type() const override { return CompressionType::NONE; }
    CMString name() const override { return "none"; }
};

std::unique_ptr<Compressor> CompressorFactory::create(CompressionType type) {
    switch (type) {
        case CompressionType::NONE:
            return std::make_unique<NoneCompressor>();
        case CompressionType::LZ4:
            // TODO: Task 3
            throw std::runtime_error("LZ4 compressor not yet implemented");
        case CompressionType::ZLIB:
            // TODO: Task 4
            throw std::runtime_error("ZLIB compressor not yet implemented");
        case CompressionType::ZSTD:
            // TODO: Task 5
            throw std::runtime_error("ZSTD compressor not yet implemented");
    }
    throw std::runtime_error("Unknown compression type");
}

std::unique_ptr<Compressor> CompressorFactory::create_from_name(const CMString& name) {
    return create(CompressorFactory::type_from_name(name));
}

CompressionType CompressorFactory::type_from_name(const CMString& name) {
    if (name == "none") return CompressionType::NONE;
    if (name == "lz4") return CompressionType::LZ4;
    if (name == "zlib") return CompressionType::ZLIB;
    if (name == "zstd") return CompressionType::ZSTD;
    throw std::runtime_error("Unknown compression type name: " + name);
}

CMString CompressorFactory::name_from_type(CompressionType type) {
    switch (type) {
        case CompressionType::NONE: return "none";
        case CompressionType::LZ4: return "lz4";
        case CompressionType::ZLIB: return "zlib";
        case CompressionType::ZSTD: return "zstd";
    }
    return "unknown";
}
```

- [ ] **Step 4: Verify build compiles and basic tests pass (NONE compressor + factory)**

Run: `cd /root/fly && bazel build //src/storage:all 2>&1 | tail -5`

Expected: BUILD SUCCESS

Run: `cd /root/fly && bazel test //src/storage/tests:compressor_test 2>&1 | tail -5`

Expected: All tests PASS

---

### Task 3: LZ4 Compressor Implementation

**Files:**
- Create: `src/storage/cpp/lz4_compressor.h`
- Create: `src/storage/cpp/lz4_compressor.cpp`
- Modify: `src/storage/cpp/compressor.cpp` (wire LZ4 into factory)
- Modify: `src/storage/tests/compressor_test.cpp` (add LZ4 tests)

- [ ] **Step 1: Write LZ4 compressor tests first**

Add to `src/storage/tests/compressor_test.cpp`:

```cpp
#include <random>
#include <storage/cpp/lz4_compressor.h>

class Lz4CompressorTest : public ::testing::Test {
protected:
    void SetUp() override {
        compressor_ = std::make_unique<Lz4Compressor>();
    }
    std::unique_ptr<Lz4Compressor> compressor_;
};

TEST_F(Lz4CompressorTest, CompressAndDecompress) {
    CMString input = "Hello, LZ4 compression! This is a test string with some repetition. repetition. repetition.";
    auto chunk = compressor_->compress(input);
    EXPECT_EQ(chunk.uncompressed_size, static_cast<int32_t>(input.size()));
    EXPECT_LT(chunk.compressed_size, static_cast<int32_t>(input.size()));
    
    auto result = compressor_->decompress(chunk.uncompressed_size, chunk.data);
    EXPECT_EQ(result, input);
}

TEST_F(Lz4CompressorTest, EmptyInput) {
    CMString input;
    auto chunk = compressor_->compress(input);
    EXPECT_EQ(chunk.uncompressed_size, 0);
    
    auto result = compressor_->decompress(chunk.uncompressed_size, chunk.data);
    EXPECT_EQ(result.size(), 0u);
}

TEST_F(Lz4CompressorTest, LargeDataRoundTrip) {
    CMString input(100000, 'x');
    input.append(100000, 'y');
    
    auto chunk = compressor_->compress(input);
    EXPECT_LT(chunk.compressed_size, static_cast<int32_t>(input.size()));
    
    auto result = compressor_->decompress(chunk.uncompressed_size, chunk.data);
    EXPECT_EQ(result, input);
}

TEST_F(Lz4CompressorTest, StreamingChunkRoundTrip) {
    CMString input(50000, 'a');
    input.append(50000, 'b');
    
    auto chunk = compressor_->compress_chunk(input);
    EXPECT_EQ(chunk.uncompressed_size, static_cast<int32_t>(input.size()));
    
    auto result = compressor_->decompress_chunk(chunk.uncompressed_size, chunk.data);
    EXPECT_EQ(result, input);
}

TEST_F(Lz4CompressorTest, FactoryCreatesLz4) {
    auto comp = CompressorFactory::create(CompressionType::LZ4);
    ASSERT_NE(comp, nullptr);
    EXPECT_EQ(comp->type(), CompressionType::LZ4);
    EXPECT_EQ(comp->name(), "lz4");
    
    CMString input = "factory test";
    auto chunk = comp->compress(input);
    auto result = comp->decompress(chunk.uncompressed_size, chunk.data);
    EXPECT_EQ(result, input);
}
```

- [ ] **Step 2: Verify tests fail (LZ4 not implemented yet)**

Run: `cd /root/fly && bazel test //src/storage/tests:compressor_test --test_filter='*Lz4*' 2>&1 | tail -5`

Expected: FAIL (LZ4Compressor not found or factory throws)

- [ ] **Step 3: Implement LZ4Compressor**

Create `src/storage/cpp/lz4_compressor.h`:

```cpp
#pragma once

#include <storage/cpp/compressor.h>
#include <string>

class Lz4Compressor : public Compressor {
public:
    explicit Lz4Compressor(int acceleration = 1);
    
    CompressedChunk compress(const CMString& input) override;
    CMString decompress(int32_t uncompressed_size, const CMString& compressed_data) override;
    CompressedChunk compress_chunk(const CMString& input) override;
    CMString decompress_chunk(int32_t uncompressed_size, const CMString& compressed_data) override;
    
    CompressionType type() const override;
    CMString name() const override;

private:
    int acceleration_;  // LZ4 acceleration factor (1 = default, higher = faster but less compression)
};
```

Create `src/storage/cpp/lz4_compressor.cpp`:

```cpp
#include <storage/cpp/lz4_compressor.h>
#include <lz4.h>
#include <stdexcept>

Lz4Compressor::Lz4Compressor(int acceleration)
    : acceleration_(acceleration) {}

CompressedChunk Lz4Compressor::compress(const CMString& input) {
    CompressedChunk chunk;
    chunk.uncompressed_size = static_cast<int32_t>(input.size());
    
    if (input.empty()) {
        chunk.compressed_size = 0;
        return chunk;
    }
    
    // LZ4_compress_default requires output buffer >= LZ4_compressBound(srcSize)
    int bound = LZ4_compressBound(static_cast<int>(input.size()));
    if (bound <= 0) {
        throw std::runtime_error("LZ4_compressBound failed");
    }
    
    CMString compressed(static_cast<size_t>(bound), '\0');
    int compressed_size = LZ4_compress_default(
        input.data(),
        compressed.data(),
        static_cast<int>(input.size()),
        bound
    );
    
    if (compressed_size <= 0) {
        throw std::runtime_error("LZ4 compression failed");
    }
    
    compressed.resize(static_cast<size_t>(compressed_size));
    chunk.data = std::move(compressed);
    chunk.compressed_size = compressed_size;
    return chunk;
}

CMString Lz4Compressor::decompress(int32_t uncompressed_size, const CMString& compressed_data) {
    if (uncompressed_size == 0) {
        return CMString();
    }
    
    CMString output(static_cast<size_t>(uncompressed_size), '\0');
    int result = LZ4_decompress_safe(
        compressed_data.data(),
        output.data(),
        static_cast<int>(compressed_data.size()),
        uncompressed_size
    );
    
    if (result < 0) {
        throw std::runtime_error("LZ4 decompression failed");
    }
    
    return output;
}

CompressedChunk Lz4Compressor::compress_chunk(const CMString& input) {
    // For LZ4, chunk compression is same as whole compression
    // LZ4 frame-level streaming is available but for our block model,
    // each block is independently compressed
    return compress(input);
}

CMString Lz4Compressor::decompress_chunk(int32_t uncompressed_size, const CMString& compressed_data) {
    // For LZ4, chunk decompression is same as whole decompression
    return decompress(uncompressed_size, compressed_data);
}

CompressionType Lz4Compressor::type() const {
    return CompressionType::LZ4;
}

CMString Lz4Compressor::name() const {
    return "lz4";
}
```

- [ ] **Step 4: Wire LZ4 into CompressorFactory**

In `src/storage/cpp/compressor.cpp`, replace the LZ4 case:

```cpp
#include <storage/cpp/lz4_compressor.h>

// In create():
case CompressionType::LZ4:
    return std::make_unique<Lz4Compressor>();
```

- [ ] **Step 5: Verify all tests pass**

Run: `cd /root/fly && bazel test //src/storage/tests:compressor_test 2>&1 | tail -10`

Expected: All tests PASS

---

### Task 4: Zlib Compressor Implementation

**Files:**
- Create: `src/storage/cpp/zlib_compressor.h`
- Create: `src/storage/cpp/zlib_compressor.cpp`
- Modify: `src/storage/cpp/compressor.cpp` (wire ZLIB into factory)
- Modify: `src/storage/tests/compressor_test.cpp` (add ZLIB tests)

- [ ] **Step 1: Write ZLIB compressor tests**

Add to `src/storage/tests/compressor_test.cpp`:

```cpp
#include <storage/cpp/zlib_compressor.h>

class ZlibCompressorTest : public ::testing::Test {
protected:
    void SetUp() override {
        compressor_ = std::make_unique<ZlibCompressor>();
    }
    std::unique_ptr<ZlibCompressor> compressor_;
};

TEST_F(ZlibCompressorTest, CompressAndDecompress) {
    CMString input = "Hello, ZLIB compression! Testing with repetitive data. Testing with repetitive data.";
    auto chunk = compressor_->compress(input);
    EXPECT_EQ(chunk.uncompressed_size, static_cast<int32_t>(input.size()));
    EXPECT_LT(chunk.compressed_size, static_cast<int32_t>(input.size()));
    
    auto result = compressor_->decompress(chunk.uncompressed_size, chunk.data);
    EXPECT_EQ(result, input);
}

TEST_F(ZlibCompressorTest, EmptyInput) {
    CMString input;
    auto chunk = compressor_->compress(input);
    EXPECT_EQ(chunk.uncompressed_size, 0);
    
    auto result = compressor_->decompress(chunk.uncompressed_size, chunk.data);
    EXPECT_EQ(result.size(), 0u);
}

TEST_F(ZlibCompressorTest, LargeDataRoundTrip) {
    CMString input(200000, 'z');
    
    auto chunk = compressor_->compress(input);
    EXPECT_LT(chunk.compressed_size, static_cast<int32_t>(input.size()));
    
    auto result = compressor_->decompress(chunk.uncompressed_size, chunk.data);
    EXPECT_EQ(result, input);
}

TEST_F(ZlibCompressorTest, FactoryCreatesZlib) {
    auto comp = CompressorFactory::create(CompressionType::ZLIB);
    ASSERT_NE(comp, nullptr);
    EXPECT_EQ(comp->type(), CompressionType::ZLIB);
    
    CMString input = "zlib factory test";
    auto chunk = comp->compress(input);
    auto result = comp->decompress(chunk.uncompressed_size, chunk.data);
    EXPECT_EQ(result, input);
}
```

- [ ] **Step 2: Implement ZlibCompressor**

Create `src/storage/cpp/zlib_compressor.h`:

```cpp
#pragma once

#include <storage/cpp/compressor.h>

class ZlibCompressor : public Compressor {
public:
    // level: 0=none, 1=fast, 6=default, 9=max compression
    explicit ZlibCompressor(int level = 6);
    
    CompressedChunk compress(const CMString& input) override;
    CMString decompress(int32_t uncompressed_size, const CMString& compressed_data) override;
    CompressedChunk compress_chunk(const CMString& input) override;
    CMString decompress_chunk(int32_t uncompressed_size, const CMString& compressed_data) override;
    
    CompressionType type() const override;
    CMString name() const override;

private:
    int level_;
};
```

Create `src/storage/cpp/zlib_compressor.cpp`:

```cpp
#include <storage/cpp/zlib_compressor.h>
#include <zlib.h>
#include <stdexcept>

ZlibCompressor::ZlibCompressor(int level)
    : level_(level) {}

CompressedChunk ZlibCompressor::compress(const CMString& input) {
    CompressedChunk chunk;
    chunk.uncompressed_size = static_cast<int32_t>(input.size());
    
    if (input.empty()) {
        chunk.compressed_size = 0;
        return chunk;
    }
    
    uLongf bound = compressBound(static_cast<uLong>(input.size()));
    CMString compressed(static_cast<size_t>(bound), '\0');
    
    uLongf dest_len = bound;
    int result = compress2(
        reinterpret_cast<Bytef*>(compressed.data()),
        &dest_len,
        reinterpret_cast<const Bytef*>(input.data()),
        static_cast<uLong>(input.size()),
        level_
    );
    
    if (result != Z_OK) {
        throw std::runtime_error("Zlib compression failed: error " + std::to_string(result));
    }
    
    compressed.resize(dest_len);
    chunk.data = std::move(compressed);
    chunk.compressed_size = static_cast<int32_t>(dest_len);
    return chunk;
}

CMString ZlibCompressor::decompress(int32_t uncompressed_size, const CMString& compressed_data) {
    if (uncompressed_size == 0) {
        return CMString();
    }
    
    CMString output(static_cast<size_t>(uncompressed_size), '\0');
    uLongf dest_len = static_cast<uLongf>(uncompressed_size);
    
    int result = uncompress(
        reinterpret_cast<Bytef*>(output.data()),
        &dest_len,
        reinterpret_cast<const Bytef*>(compressed_data.data()),
        static_cast<uLong>(compressed_data.size())
    );
    
    if (result != Z_OK) {
        throw std::runtime_error("Zlib decompression failed: error " + std::to_string(result));
    }
    
    return output;
}

CompressedChunk ZlibCompressor::compress_chunk(const CMString& input) {
    return compress(input);
}

CMString ZlibCompressor::decompress_chunk(int32_t uncompressed_size, const CMString& compressed_data) {
    return decompress(uncompressed_size, compressed_data);
}

CompressionType ZlibCompressor::type() const {
    return CompressionType::ZLIB;
}

CMString ZlibCompressor::name() const {
    return "zlib";
}
```

- [ ] **Step 3: Wire ZLIB into CompressorFactory**

In `src/storage/cpp/compressor.cpp`:

```cpp
#include <storage/cpp/zlib_compressor.h>

// In create():
case CompressionType::ZLIB:
    return std::make_unique<ZlibCompressor>();
```

- [ ] **Step 4: Verify tests pass**

Run: `cd /root/fly && bazel test //src/storage/tests:compressor_test 2>&1 | tail -10`

Expected: All tests PASS (now includes LZ4 + ZLIB)

---

### Task 5: Zstd Compressor Implementation

**Files:**
- Create: `src/storage/cpp/zstd_compressor.h`
- Create: `src/storage/cpp/zstd_compressor.cpp`
- Modify: `src/storage/cpp/compressor.cpp` (wire ZSTD into factory)
- Modify: `src/storage/tests/compressor_test.cpp` (add ZSTD tests)

- [ ] **Step 1: Write ZSTD compressor tests**

Add to `src/storage/tests/compressor_test.cpp`:

```cpp
#include <storage/cpp/zstd_compressor.h>

class ZstdCompressorTest : public ::testing::Test {
protected:
    void SetUp() override {
        compressor_ = std::make_unique<ZstdCompressor>();
    }
    std::unique_ptr<ZstdCompressor> compressor_;
};

TEST_F(ZstdCompressorTest, CompressAndDecompress) {
    CMString input = "Hello, ZSTD compression! High compression ratio test data here.";
    auto chunk = compressor_->compress(input);
    EXPECT_EQ(chunk.uncompressed_size, static_cast<int32_t>(input.size()));
    EXPECT_LT(chunk.compressed_size, static_cast<int32_t>(input.size()));
    
    auto result = compressor_->decompress(chunk.uncompressed_size, chunk.data);
    EXPECT_EQ(result, input);
}

TEST_F(ZstdCompressorTest, EmptyInput) {
    CMString input;
    auto chunk = compressor_->compress(input);
    EXPECT_EQ(chunk.uncompressed_size, 0);
    
    auto result = compressor_->decompress(chunk.uncompressed_size, chunk.data);
    EXPECT_EQ(result.size(), 0u);
}

TEST_F(ZstdCompressorTest, LargeDataRoundTrip) {
    CMString input(200000, 'q');
    
    auto chunk = compressor_->compress(input);
    EXPECT_LT(chunk.compressed_size, static_cast<int32_t>(input.size()));
    
    auto result = compressor_->decompress(chunk.uncompressed_size, chunk.data);
    EXPECT_EQ(result, input);
}

TEST_F(ZstdCompressorTest, FactoryCreatesZstd) {
    auto comp = CompressorFactory::create(CompressionType::ZSTD);
    ASSERT_NE(comp, nullptr);
    EXPECT_EQ(comp->type(), CompressionType::ZSTD);
    
    CMString input = "zstd factory test";
    auto chunk = comp->compress(input);
    auto result = comp->decompress(chunk.uncompressed_size, chunk.data);
    EXPECT_EQ(result, input);
}
```

- [ ] **Step 2: Implement ZstdCompressor**

Create `src/storage/cpp/zstd_compressor.h`:

```cpp
#pragma once

#include <storage/cpp/compressor.h>

class ZstdCompressor : public Compressor {
public:
    // level: 1=fast, 3=default, 22=max
    explicit ZstdCompressor(int level = 3);
    
    CompressedChunk compress(const CMString& input) override;
    CMString decompress(int32_t uncompressed_size, const CMString& compressed_data) override;
    CompressedChunk compress_chunk(const CMString& input) override;
    CMString decompress_chunk(int32_t uncompressed_size, const CMString& compressed_data) override;
    
    CompressionType type() const override;
    CMString name() const override;

private:
    int level_;
};
```

Create `src/storage/cpp/zstd_compressor.cpp`:

```cpp
#include <storage/cpp/zstd_compressor.h>
#include <zstd.h>
#include <stdexcept>

ZstdCompressor::ZstdCompressor(int level)
    : level_(level) {}

CompressedChunk ZstdCompressor::compress(const CMString& input) {
    CompressedChunk chunk;
    chunk.uncompressed_size = static_cast<int32_t>(input.size());
    
    if (input.empty()) {
        chunk.compressed_size = 0;
        return chunk;
    }
    
    size_t bound = ZSTD_compressBound(input.size());
    CMString compressed(bound, '\0');
    
    size_t result = ZSTD_compress(
        compressed.data(),
        bound,
        input.data(),
        input.size(),
        level_
    );
    
    if (ZSTD_isError(result)) {
        throw std::runtime_error("ZSTD compression failed: " + std::string(ZSTD_getErrorName(result)));
    }
    
    compressed.resize(result);
    chunk.data = std::move(compressed);
    chunk.compressed_size = static_cast<int32_t>(result);
    return chunk;
}

CMString ZstdCompressor::decompress(int32_t uncompressed_size, const CMString& compressed_data) {
    if (uncompressed_size == 0) {
        return CMString();
    }
    
    CMString output(static_cast<size_t>(uncompressed_size), '\0');
    
    size_t result = ZSTD_decompress(
        output.data(),
        static_cast<size_t>(uncompressed_size),
        compressed_data.data(),
        compressed_data.size()
    );
    
    if (ZSTD_isError(result)) {
        throw std::runtime_error("ZSTD decompression failed: " + std::string(ZSTD_getErrorName(result)));
    }
    
    return output;
}

CompressedChunk ZstdCompressor::compress_chunk(const CMString& input) {
    return compress(input);
}

CMString ZstdCompressor::decompress_chunk(int32_t uncompressed_size, const CMString& compressed_data) {
    return decompress(uncompressed_size, compressed_data);
}

CompressionType ZstdCompressor::type() const {
    return CompressionType::ZSTD;
}

CMString ZstdCompressor::name() const {
    return "zstd";
}
```

- [ ] **Step 3: Wire ZSTD into CompressorFactory**

In `src/storage/cpp/compressor.cpp`:

```cpp
#include <storage/cpp/zstd_compressor.h>

// In create():
case CompressionType::ZSTD:
    return std::make_unique<ZstdCompressor>();
```

- [ ] **Step 4: Verify all compressor tests pass**

Run: `cd /root/fly && bazel test //src/storage/tests:compressor_test 2>&1 | tail -10`

Expected: ALL tests PASS (NONE + LZ4 + ZLIB + ZSTD)

---

### Task 6: Compression Utilities - Chunk I/O Format

**Files:**
- Create: `src/storage/cpp/compression_utils.h`
- Create: `src/storage/cpp/compression_utils.cpp`
- Modify: `src/storage/tests/compressor_test.cpp` (add chunk I/O tests)

The on-disk format for a compressed chunk is:
```
[int32_t uncompressed_size][int32_t compressed_size][compressed_bytes...]
```

This format allows DataReader to:
1. Read `int32_t uncompressed_size` (4 bytes)
2. Read `int32_t compressed_size` (4 bytes)
3. Read exactly `compressed_size` bytes
4. Decompress those bytes using the algorithm specified in IndexEntry

- [ ] **Step 1: Create compression_utils.h**

```cpp
#pragma once

#include <storage/cpp/compressor.h>
#include <common/cpp/common_types.h>
#include <cstdint>
#include <fstream>

namespace compression_utils {

// Serialize a CompressedChunk to binary format:
// [int32_t uncompressed_size][int32_t compressed_size][compressed_bytes...]
// Returns the full serialized chunk including header.
CMString serialize_chunk(const CompressedChunk& chunk);

// Parse a compressed chunk from binary data at the given offset.
// Returns the parsed chunk and advances offset past the chunk.
CompressedChunk deserialize_chunk(const CMString& data, int64_t& offset);

// Write a compressed chunk directly to an output stream.
void write_chunk_to_stream(const CompressedChunk& chunk, std::ofstream& ofs);

// Write a compressed chunk to an ofstream and return the total bytes written.
int64_t write_compressed_to_stream(const CompressedChunk& chunk, std::ofstream& ofs);

// Read a compressed chunk from an ifstream at the given offset.
// Returns the parsed chunk.
CompressedChunk read_compressed_from_stream(std::ifstream& ifs, int64_t offset);

} // namespace compression_utils
```

- [ ] **Step 2: Create compression_utils.cpp**

```cpp
#include <storage/cpp/compression_utils.h>
#include <stdexcept>

namespace compression_utils {

CMString serialize_chunk(const CompressedChunk& chunk) {
    // Header: 4 bytes uncompressed_size + 4 bytes compressed_size
    CMString result;
    int32_t uncompressed_size = chunk.uncompressed_size;
    int32_t compressed_size = chunk.compressed_size;
    
    result.resize(sizeof(int32_t) * 2 + static_cast<size_t>(compressed_size));
    
    // Write header
    std::memcpy(result.data(), &uncompressed_size, sizeof(int32_t));
    std::memcpy(result.data() + sizeof(int32_t), &compressed_size, sizeof(int32_t));
    
    // Write compressed data
    std::memcpy(result.data() + sizeof(int32_t) * 2, chunk.data.data(), static_cast<size_t>(compressed_size));
    
    return result;
}

CompressedChunk deserialize_chunk(const CMString& data, int64_t& offset) {
    CompressedChunk chunk;
    
    if (static_cast<int64_t>(data.size()) < offset + static_cast<int64_t>(sizeof(int32_t) * 2)) {
        throw std::runtime_error("Insufficient data for chunk header");
    }
    
    // Read header
    std::memcpy(&chunk.uncompressed_size, data.data() + offset, sizeof(int32_t));
    std::memcpy(&chunk.compressed_size, data.data() + offset + sizeof(int32_t), sizeof(int32_t));
    offset += sizeof(int32_t) * 2;
    
    // Read compressed data
    if (static_cast<int64_t>(data.size()) < offset + static_cast<int64_t>(chunk.compressed_size)) {
        throw std::runtime_error("Insufficient data for chunk payload");
    }
    
    chunk.data.assign(data.data() + offset, static_cast<size_t>(chunk.compressed_size));
    offset += static_cast<int64_t>(chunk.compressed_size);
    
    return chunk;
}

void write_chunk_to_stream(const CompressedChunk& chunk, std::ofstream& ofs) {
    CMString serialized = serialize_chunk(chunk);
    ofs.write(serialized.data(), static_cast<std::streamsize>(serialized.size()));
}

int64_t write_compressed_to_stream(const CompressedChunk& chunk, std::ofstream& ofs) {
    int32_t uncompressed_size = chunk.uncompressed_size;
    int32_t compressed_size = chunk.compressed_size;
    
    ofs.write(reinterpret_cast<const char*>(&uncompressed_size), sizeof(int32_t));
    ofs.write(reinterpret_cast<const char*>(&compressed_size), sizeof(int32_t));
    ofs.write(chunk.data.data(), static_cast<std::streamsize>(compressed_size));
    
    return sizeof(int32_t) * 2 + static_cast<int64_t>(compressed_size);
}

CompressedChunk read_compressed_from_stream(std::ifstream& ifs, int64_t offset) {
    CompressedChunk chunk;
    
    ifs.seekg(offset);
    
    ifs.read(reinterpret_cast<char*>(&chunk.uncompressed_size), sizeof(int32_t));
    ifs.read(reinterpret_cast<char*>(&chunk.compressed_size), sizeof(int32_t));
    
    chunk.data.resize(static_cast<size_t>(chunk.compressed_size));
    ifs.read(chunk.data.data(), static_cast<std::streamsize>(chunk.compressed_size));
    
    return chunk;
}

} // namespace compression_utils
```

- [ ] **Step 3: Add compression_utils tests**

Add to `src/storage/tests/compressor_test.cpp`:

```cpp
#include <storage/cpp/compression_utils.h>

TEST(CompressionUtilsTest, SerializeDeserializeChunk) {
    auto compressor = CompressorFactory::create(CompressionType::LZ4);
    CMString input = "Test data for serialize/deserialize";
    auto chunk = compressor->compress(input);
    
    CMString serialized = compression_utils::serialize_chunk(chunk);
    EXPECT_FALSE(serialized.empty());
    
    int64_t offset = 0;
    auto deserialized = compression_utils::deserialize_chunk(serialized, offset);
    
    EXPECT_EQ(deserialized.uncompressed_size, chunk.uncompressed_size);
    EXPECT_EQ(deserialized.compressed_size, chunk.compressed_size);
    EXPECT_EQ(deserialized.data, chunk.data);
    
    auto result = compressor->decompress(deserialized.uncompressed_size, deserialized.data);
    EXPECT_EQ(result, input);
}

TEST(CompressionUtilsTest, RoundTripThroughFile) {
    auto compressor = CompressorFactory::create(CompressionType::LZ4);
    CMString input = "Test data for file I/O round trip";
    auto chunk = compressor->compress(input);
    
    CMString test_file = "/tmp/fly_compression_test.dat";
    std::ofstream ofs(test_file, std::ios::binary);
    int64_t bytes_written = compression_utils::write_compressed_to_stream(chunk, ofs);
    ofs.close();
    
    std::ifstream ifs(test_file, std::ios::binary);
    auto read_chunk = compression_utils::read_compressed_from_stream(ifs, 0);
    ifs.close();
    
    EXPECT_EQ(read_chunk.uncompressed_size, chunk.uncompressed_size);
    EXPECT_EQ(read_chunk.compressed_size, chunk.compressed_size);
    EXPECT_EQ(read_chunk.data, chunk.data);
    
    auto result = compressor->decompress(read_chunk.uncompressed_size, read_chunk.data);
    EXPECT_EQ(result, input);
    
    std::remove(test_file.c_str());
}
```

- [ ] **Step 4: Verify build and tests**

Run: `cd /root/fly && bazel build //src/storage:all && bazel test //src/storage/tests:compressor_test 2>&1 | tail -10`

Expected: BUILD SUCCESS, ALL tests PASS

---

### Task 7: Add Compression Config to Config Singleton

**Files:**
- Modify: `src/core/cpp/config.h`
- Modify: `src/core/cpp/config.cpp`
- Modify: `src/core/tests/config_test.cpp` (add compression config tests)

- [ ] **Step 1: Add compression config defaults**

In `src/core/cpp/config.cpp`, add to `INT_DEFAULTS`:

```cpp
const CMMap<CMString, int64_t> Config::INT_DEFAULTS = {
    // ... existing entries ...
    {"compression_level", 0},           // 0=default for each algorithm
    {"compression_threshold", 128},      // Don't compress objects < 128 bytes
};

const CMMap<CMString, CMString> Config::STR_DEFAULTS = {
    {"transport_type", "tcp"},
    {"compression_type", "lz4"},        // lz4, zlib, zstd, none
};
```

- [ ] **Step 2: Verify existing config tests still pass**

Run: `cd /root/fly && bazel test //src/core/tests:config_test 2>&1 | tail -5`

Expected: All tests PASS

- [ ] **Step 3: Add compression config tests**

Add to `src/core/tests/config_test.cpp`:

```cpp
TEST(ConfigTest, CompressionTypeDefault) {
    Config::instance().reset();
    EXPECT_EQ(Config::instance().get_str("compression_type"), "lz4");
}

TEST(ConfigTest, CompressionLevelDefault) {
    Config::instance().reset();
    EXPECT_EQ(Config::instance().get_int("compression_level"), 0);
}

TEST(ConfigTest, CompressionThresholdDefault) {
    Config::instance().reset();
    EXPECT_EQ(Config::instance().get_int("compression_threshold"), 128);
}

TEST(ConfigTest, SetCompressionType) {
    Config::instance().reset();
    Config::instance().set_str("compression_type", "zstd");
    EXPECT_EQ(Config::instance().get_str("compression_type"), "zstd");
}

TEST(ConfigTest, SetCompressionLevel) {
    Config::instance().reset();
    Config::instance().set_int("compression_level", 9);
    EXPECT_EQ(Config::instance().get_int("compression_level"), 9);
}
```

- [ ] **Step 4: Verify config tests pass**

Run: `cd /root/fly && bazel test //src/core/tests:config_test 2>&1 | tail -5`

Expected: All tests PASS

---

### Task 8: Update IndexEntry with compression_type

**Files:**
- Modify: `src/storage/cpp/index_entry.h`
- Modify: `src/storage/cpp/local_index.cpp` (re-serialization with new field)
- Modify: `src/storage/cpp/db_meta.h` (add compression_type to WorkerInfo? No - compression is per-object)

- [ ] **Step 1: Add `compression_type` field to IndexEntry**

Update `src/storage/cpp/index_entry.h`:

```cpp
#pragma once

#include <common/cpp/common_types.h>
#include <serialization/cpp/serialization_macros.h>
#include <cstdint>

struct IndexEntry {
    CMString object_name;
    CMString file_name;
    int64_t offset = 0;
    int64_t size = 0;
    bool is_large = false;
    int32_t block_count = 0;
    int8_t compression_type = 0;  // CompressionType enum value (0=NONE, 1=LZ4, 2=ZLIB, 3=ZSTD)

    FLY_SERIALIZE_MEMBER_COUNT(7);  // Changed from 6 to 7
};
```

**IMPORTANT:** This changes the serialization format. We need a versioning strategy or migration path. Since this is a pre-release project with no production data, bumping the member count is acceptable.

- [ ] **Step 2: Verify build after IndexEntry change**

Run: `cd /root/fly && bazel build //src/storage:all 2>&1 | tail -5`

Expected: BUILD SUCCESS

- [ ] **Step 3: Run all storage tests to check for breakage**

Run: `cd /root/fly && bazel test //src/storage/tests:all 2>&1 | tail -10`

Expected: All tests PASS (empty DB has no existing index files)

---

### Task 9: Update DataWriter to Compress Before Writing

**Files:**
- Modify: `src/storage/cpp/data_writer.h`
- Modify: `src/storage/cpp/data_writer.cpp`

- [ ] **Step 1: Add Compressor member and compression_threshold to DataWriter**

Update `src/storage/cpp/data_writer.h`:

```cpp
#pragma once

#include <storage/cpp/local_index.h>
#include <storage/cpp/compressor.h>
#include <common/cpp/common_types.h>
#include <cstdint>
#include <fstream>
#include <memory>

class DataWriter {
public:
    DataWriter(
        const CMString& base_path,
        const CMString& data_path,
        uint64_t worker_id,
        int64_t aggregation_threshold,
        int64_t large_file_threshold,
        int64_t block_size,
        CompressionType compression_type = CompressionType::LZ4,
        int64_t compression_threshold = 128,
        int compression_level = 0
    );

    ~DataWriter();

    DataWriter(const DataWriter&) = delete;
    DataWriter& operator=(const DataWriter&) = delete;

    CMString write_object(const CMString& object_name, const CMString& data, bool backup = false);

    void flush();
    void close();

    int64_t total_bytes_written() const;
    int32_t file_count() const;

private:
    void create_new_file();
    CMString get_current_file_name();
    void write_small_object(const CMString& object_name, const CMString& data);
    void write_large_object(const CMString& object_name, const CMString& data);

    CMString base_path_;
    CMString data_path_;
    uint64_t worker_id_;
    int64_t aggregation_threshold_;
    int64_t large_file_threshold_;
    int64_t block_size_;

    CompressionType compression_type_;
    int64_t compression_threshold_;
    std::unique_ptr<Compressor> compressor_;

    CMString current_file_;
    int32_t file_index_ = 1;
    int64_t current_file_size_ = 0;
    std::ofstream file_stream_;

    std::unique_ptr<LocalIndex> index_;
    int64_t total_bytes_ = 0;
    bool closed_ = false;
};
```

- [ ] **Step 2: Update DataWriter constructor to initialize compressor**

Update `src/storage/cpp/data_writer.cpp` constructor:

```cpp
DataWriter::DataWriter(
    const CMString& base_path,
    const CMString& data_path,
    uint64_t worker_id,
    int64_t aggregation_threshold,
    int64_t large_file_threshold,
    int64_t block_size,
    CompressionType compression_type,
    int64_t compression_threshold,
    int compression_level
)
    : base_path_(base_path)
    , data_path_(data_path)
    , worker_id_(worker_id)
    , aggregation_threshold_(aggregation_threshold)
    , large_file_threshold_(large_file_threshold)
    , block_size_(block_size)
    , compression_type_(compression_type)
    , compression_threshold_(compression_threshold) {
    
    if (compression_type != CompressionType::NONE) {
        compressor_ = CompressorFactory::create(compression_type);
    }
    
    // ... rest of constructor unchanged ...
}
```

- [ ] **Step 3: Update `write_small_object` to compress**

```cpp
void DataWriter::write_small_object(const CMString& object_name, const CMString& data) {
    CMString data_to_write;
    int8_t comp_type = static_cast<int8_t>(CompressionType::NONE);
    
    if (compressor_ && static_cast<int64_t>(data.size()) >= compression_threshold_) {
        auto chunk = compressor_->compress(data);
        data_to_write = compression_utils::serialize_chunk(chunk);
        comp_type = static_cast<int8_t>(compression_type_);
    } else {
        data_to_write = data;
    }
    
    if (current_file_size_ + static_cast<int64_t>(data_to_write.size()) > aggregation_threshold_ && current_file_size_ > 0) {
        file_index_++;
        create_new_file();
    }
    
    int64_t offset = current_file_size_;
    
    file_stream_.write(data_to_write.data(), static_cast<std::streamsize>(data_to_write.size()));
    current_file_size_ += static_cast<int64_t>(data_to_write.size());
    
    IndexEntry entry{object_name, current_file_, offset, static_cast<int64_t>(data_to_write.size()), false, 0, comp_type};
    index_->add_entry(entry);
}
```

- [ ] **Step 4: Update `write_large_object` to compress per-block**

```cpp
void DataWriter::write_large_object(const CMString& object_name, const CMString& data) {
    int64_t total_size = static_cast<int64_t>(data.size());
    int32_t block_count = static_cast<int32_t>((total_size + block_size_ - 1) / block_size_);
    int8_t comp_type = static_cast<int8_t>(compression_type_);
    
    int64_t offset = 0;
    for (int32_t i = 0; i < block_count; ++i) {
        int64_t remaining = total_size - offset;
        int64_t block_data_size = std::min(remaining, block_size_);
        CMString block_data(data.data() + offset, static_cast<size_t>(block_data_size));
        
        CMString data_to_write;
        if (compressor_ && static_cast<int64_t>(block_data.size()) >= compression_threshold_) {
            auto chunk = compressor_->compress_chunk(block_data);
            data_to_write = compression_utils::serialize_chunk(chunk);
        } else {
            // Still write uncompressed block with header for format consistency
            CompressedChunk raw_chunk;
            raw_chunk.uncompressed_size = static_cast<int32_t>(block_data.size());
            raw_chunk.compressed_size = static_cast<int32_t>(block_data.size());
            raw_chunk.data = block_data;
            data_to_write = compression_utils::serialize_chunk(raw_chunk);
            comp_type = static_cast<int8_t>(CompressionType::NONE);
        }
        
        if (current_file_size_ + static_cast<int64_t>(data_to_write.size()) > aggregation_threshold_ && current_file_size_ > 0) {
            file_index_++;
            create_new_file();
        }
        
        int64_t file_offset = current_file_size_;
        file_stream_.write(data_to_write.data(), static_cast<std::streamsize>(data_to_write.size()));
        current_file_size_ += static_cast<int64_t>(data_to_write.size());
        
        IndexEntry block_entry{object_name, current_file_, file_offset, static_cast<int64_t>(data_to_write.size()), true, block_count, comp_type};
        index_->add_entry(block_entry);
        
        offset += block_data_size;
    }
}
```

- [ ] **Step 5: Add `#include <storage/cpp/compression_utils.h>` to data_writer.cpp**

Add at top of `data_writer.cpp`:
```cpp
#include <storage/cpp/compression_utils.h>
```

- [ ] **Step 6: Verify build**

Run: `cd /root/fly && bazel build //src/storage:all 2>&1 | tail -5`

Expected: BUILD SUCCESS

---

### Task 10: Update DataReader to Decompress After Reading

**Files:**
- Modify: `src/storage/cpp/data_reader.h`
- Modify: `src/storage/cpp/data_reader.cpp`

- [ ] **Step 1: Add Compressor and decompression logic to DataReader**

Update `src/storage/cpp/data_reader.h`:

```cpp
#pragma once

#include <storage/cpp/local_index.h>
#include <storage/cpp/compressor.h>
#include <common/cpp/common_types.h>
#include <cstdint>
#include <fstream>
#include <memory>

class DataReader {
public:
    DataReader(
        const CMString& base_path,
        const CMString& data_path,
        uint64_t worker_id
    );

    ~DataReader();

    DataReader(const DataReader&) = delete;
    DataReader& operator=(const DataReader&) = delete;

    CMString read_object(const CMString& object_name);
    CMString read_object(const IndexEntry& entry);

    bool exists(const CMString& object_name);

private:
    CMString find_file_path(const CMString& file_name);
    CMString read_from_file(const CMString& file_path, int64_t offset, int64_t size);
    CMString read_large_object(const IndexEntry& entry);
    CMString decompress_data(const CMString& compressed_data, int8_t compression_type);

    CMString base_path_;
    CMString data_path_;
    uint64_t worker_id_;

    std::unique_ptr<LocalIndex> index_;
};
```

- [ ] **Step 2: Update data_reader.cpp with decompression**

The `read_object(IndexEntry&)` method now needs to check `compression_type` in the IndexEntry and decompress if needed.

For small objects:
1. Read raw bytes from file
2. If `compression_type != NONE`, deserialize chunk header, then decompress
3. If `compression_type == NONE`, return raw bytes directly (backward compatible: old format has no chunk header)

For large objects:
1. Each block is individually compressed (with chunk header)
2. Read each block, deserialize chunk header, decompress
3. Concatenate decompressed blocks

Add `decompress_data` method:

```cpp
CMString DataReader::decompress_data(const CMString& raw_data, int8_t compression_type) {
    if (compression_type == static_cast<int8_t>(CompressionType::NONE)) {
        // Check if this is the new format (has chunk header) or old format (raw data)
        // New format: first 4 bytes are uncompressed_size, next 4 bytes are compressed_size
        // If compressed_size matches remaining data length, it's a chunk header
        if (raw_data.size() >= sizeof(int32_t) * 2) {
            int32_t uncompressed_size;
            int32_t compressed_size;
            std::memcpy(&uncompressed_size, raw_data.data(), sizeof(int32_t));
            std::memcpy(&compressed_size, raw_data.data() + sizeof(int32_t), sizeof(int32_t));
            
            int64_t expected_total = sizeof(int32_t) * 2 + static_cast<int64_t>(compressed_size);
            if (expected_total == static_cast<int64_t>(raw_data.size())) {
                // This is a chunk format - read without compression
                int64_t offset = 0;
                auto chunk = compression_utils::deserialize_chunk(CMString(raw_data), offset);
                return chunk.data;  // uncompressed data
            }
        }
        // Old raw format - return as-is
        return raw_data;
    }
    
    // Deserialize the chunk header and decompress
    int64_t offset = 0;
    auto chunk = compression_utils::deserialize_chunk(CMString(raw_data), offset);
    
    auto compressor = CompressorFactory::create(static_cast<CompressionType>(compression_type));
    return compressor->decompress(chunk.uncompressed_size, chunk.data);
}
```

Update `read_object`:

```cpp
CMString DataReader::read_object(const IndexEntry& entry) {
    if (entry.is_large) {
        return read_large_object(entry);
    }
    
    CMString file_path = find_file_path(entry.file_name);
    CMString raw_data = read_from_file(file_path, entry.offset, entry.size);
    return decompress_data(raw_data, entry.compression_type);
}
```

Update `read_large_object`:

```cpp
CMString DataReader::read_large_object(const IndexEntry& first_entry) {
    CMString object_name = first_entry.object_name;
    CMVector<IndexEntry>* all_blocks = index_->find_all_entries(object_name);
    if (!all_blocks || all_blocks->empty()) {
        throw std::runtime_error("No blocks found for large object: " + object_name);
    }
    
    CMVector<IndexEntry> blocks = *all_blocks;
    std::sort(blocks.begin(), blocks.end(),
        [](const IndexEntry& a, const IndexEntry& b) {
            if (a.file_name != b.file_name) return a.file_name < b.file_name;
            return a.offset < b.offset;
        });
    
    CMString result;
    for (const auto& block : blocks) {
        CMString file_path = find_file_path(block.file_name);
        CMString raw_data = read_from_file(file_path, block.offset, block.size);
        CMString decompressed = decompress_data(raw_data, block.compression_type);
        result += decompressed;
    }
    
    return result;
}
```

Add include:
```cpp
#include <storage/cpp/compression_utils.h>
```

- [ ] **Step 3: Verify build**

Run: `cd /root/fly && bazel build //src/storage:all 2>&1 | tail -5`

Expected: BUILD SUCCESS

---

### Task 11: Update Database to Pass Compression Config to DataWriter

**Files:**
- Modify: `src/storage/cpp/database.h`
- Modify: `src/storage/cpp/database.cpp`

- [ ] **Step 1: Update Database constructor to accept and pass compression config**

The Database should read compression config from Config singleton and pass it to DataWriter.

Update `database.h` to include compressor header:

```cpp
#include <storage/cpp/compressor.h>
```

Update `database.cpp` constructor — the DataWriter constructor now takes `compression_type`, `compression_threshold`, and `compression_level`:

```cpp
Config& config = Config::instance();
CMString comp_type_str = config.get_str("compression_type");
CompressionType comp_type = CompressorFactory::type_from_name(comp_type_str);
int64_t comp_threshold = config.get_int("compression_threshold");
int comp_level = static_cast<int>(config.get_int("compression_level"));

writer_ = std::make_unique<DataWriter>(
    base_path_, data_path_, 0,
    config.get_int("aggregation_threshold"),
    config.get_int("large_file_threshold"),
    config.get_int("block_size"),
    comp_type,
    comp_threshold,
    comp_level
);
```

- [ ] **Step 2: Verify build**

Run: `cd /root/fly && bazel build //src/storage:all 2>&1 | tail -5`

Expected: BUILD SUCCESS

---

### Task 12: Update Python Export for Compression Config

**Files:**
- Modify: `src/storage/export/storage_export.cpp`

- [ ] **Step 1: Add CompressionType enum and compression config exposure**

Add to `storage_export.cpp`:

```cpp
#include <storage/cpp/compressor.h>

// In the NB_MODULE block:
nb::enum_<CompressionType>(m, "CompressionType")
    .value("NONE", CompressionType::NONE)
    .value("LZ4", CompressionType::LZ4)
    .value("ZLIB", CompressionType::ZLIB)
    .value("ZSTD", CompressionType::ZSTD);

m.def("compression_type_from_name", &CompressorFactory::type_from_name);
m.def("compression_name_from_type", &CompressorFactory::name_from_type);
```

Also update the Database Python bindings if needed to reflect new constructor.

- [ ] **Step 2: Verify build**

Run: `cd /root/fly && bazel build //src/storage:all 2>&1 | tail -5`

Expected: BUILD SUCCESS

---

### Task 13: Integration Test - End-to-End Compression

**Files:**
- Create: `src/storage/tests/compression_integration_test.cpp`

- [ ] **Step 1: Write end-to-end integration test**

```cpp
#include <gtest/gtest.h>
#include <storage/cpp/database.h>
#include <storage/cpp/data_writer.h>
#include <storage/cpp/data_reader.h>
#include <storage/cpp/compressor.h>
#include <storage/cpp/compression_utils.h>
#include <core/cpp/config.h>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

class CompressionIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = "/tmp/fly_compression_test_" + std::to_string(::getpid());
        fs::create_directories(test_dir_);
    }
    
    void TearDown() override {
        fs::remove_all(test_dir_);
        Config::instance().reset();
    }
    
    CMString test_dir_;
};

TEST_F(CompressionIntegrationTest, WriteAndReadWithLz4) {
    Config::instance().set_str("compression_type", "lz4");
    Config::instance().set_int("compression_threshold", 32);  // low threshold so everything gets compressed
    
    Database db(test_dir_, "");
    db.write_object("test_key", "Hello, LZ4 compression world!");
    
    CMString result = db.read_object("test_key");
    EXPECT_EQ(result, "Hello, LZ4 compression world!");
}

TEST_F(CompressionIntegrationTest, WriteAndReadWithZlib) {
    Config::instance().set_str("compression_type", "zlib");
    Config::instance().set_int("compression_threshold", 32);
    
    Database db(test_dir_, "");
    db.write_object("zlib_key", "Test data for ZLIB compression");
    
    CMString result = db.read_object("zlib_key");
    EXPECT_EQ(result, "Test data for ZLIB compression");
}

TEST_F(CompressionIntegrationTest, WriteAndReadWithZstd) {
    Config::instance().set_str("compression_type", "zstd");
    Config::instance().set_int("compression_threshold", 32);
    
    Database db(test_dir_, "");
    db.write_object("zstd_key", "Test data for ZSTD compression");
    
    CMString result = db.read_object("zstd_key");
    EXPECT_EQ(result, "Test data for ZSTD compression");
}

TEST_F(CompressionIntegrationTest, NoCompressionWhenBelowThreshold) {
    Config::instance().set_str("compression_type", "lz4");
    Config::instance().set_int("compression_threshold", 10000);  // high threshold - nothing gets compressed
    
    Database db(test_dir_, "");
    db.write_object("small_key", "tiny");
    
    CMString result = db.read_object("small_key");
    EXPECT_EQ(result, "tiny");
}

TEST_F(CompressionIntegrationTest, LargeObjectCompression) {
    Config::instance().set_str("compression_type", "lz4");
    Config::instance().set_int("compression_threshold", 32);
    Config::instance().set_int("block_size", 256);  // small block size for testing
    
    Database db(test_dir_, "");
    
    CMString large_data(10000, 'A');
    db.write_object("large_key", large_data);
    
    CMString result = db.read_object("large_key");
    EXPECT_EQ(result, large_data);
}

TEST_F(CompressionIntegrationTest, FreezeAndReadBack) {
    Config::instance().set_str("compression_type", "lz4");
    Config::instance().set_int("compression_threshold", 32);
    
    {
        Database db(test_dir_, "");
        db.write_object("freeze_key", "Data before freeze");
        db.freeze();
    }
    
    // Read back from frozen database
    // Note: would need a separate read-only path, but for now test index persistence
    fs::path idx_path = fs::path(test_dir_) / "worker_0.idx";
    EXPECT_TRUE(fs::exists(idx_path));
}
```

- [ ] **Step 2: Add BUILD target for integration test**

Update BUILD file (will be auto-generated by fly.sh, but add the test cc_test target).

- [ ] **Step 3: Run integration tests**

Run: `cd /root/fly && bazel test //src/storage/tests:compression_integration_test 2>&1 | tail -10`

Expected: All tests PASS

---

### Task 14: Update Existing Tests and Full Test Suite Verification

**Files:**
- Modify: `src/storage/tests/data_writer_test.cpp` (update constructor calls)
- Modify: `src/storage/tests/data_reader_test.cpp` (update for compression)
- Modify: `src/storage/tests/database_test.cpp` (update for compression)

- [ ] **Step 1: Update DataWriter test constructor calls**

All `DataWriter` constructor calls need the 3 new parameters. Add defaults:
```cpp
DataWriter writer(base_path, data_path, 0, 4096, 1048576, 65536, 
                  CompressionType::LZ4, 128, 0);
```

Or use `CompressionType::NONE` to test old (uncompressed) behavior.

- [ ] **Step 2: Verify all existing storage tests pass with default compression**

Run: `cd /root/fly && bazel test //src/storage/tests:all 2>&1 | tail -10`

Expected: All tests PASS

- [ ] **Step 3: Run ALL project tests**

Run: `cd /root/fly && bazel test //... 2>&1 | tail -20`

Expected: All tests PASS

- [ ] **Step 4: Run Python integration tests**

Run: `cd /root/fly && python3 -m pytest qa/storage_test.py -v 2>&1 | tail -20`

Expected: All pytest PASS

---

### Task 15: Update BUILD File and Final Verification

**Files:**
- Regenerate: `BUILD` (via `fly.sh build`)
- Verify all targets compile and test

- [ ] **Step 1: Run fly.sh build to regenerate BUILD**

Run: `cd /root/fly && ./fly.sh build 2>&1 | tail -20`

Expected: All targets built, compile_commands.json updated

- [ ] **Step 2: Run full test suite**

Run: `cd /root/fly && bazel test //... 2>&1 | tail -20`

Expected: All tests PASS

- [ ] **Step 3: Run Python integration tests**

Run: `cd /root/fly && python3 -m pytest qa/storage_test.py -v 2>&1 | tail -20`

Expected: All pytest PASS

- [ ] **Step 4: Verify clangd compiles without errors**

Run: `cd /root/fly && ./fly.sh build 2>&1 | tail -5`

Check LSP diagnostics on changed files.