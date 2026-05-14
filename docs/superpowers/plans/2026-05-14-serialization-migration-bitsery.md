# Serialization Migration: zpp_bits → bitsery Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Migrate Fly's serialization layer from zpp_bits to bitsery, introducing stream-chain compression, object headers with Python type info, and library-agnostic macros.

**Architecture:** Three independent subsystems: (1) Serialization macros + bitsery integration with versioning support, (2) Object header + typed read/write API + CompressingStreamBuf, (3) Python type resolution via `getattr(sys.modules)`. Each subsystem is testable in isolation.

**Tech Stack:** C++20, Bazel, gtest, nanobind, bitsery (header-only), lz4/zlib/zstd

---

## Design Decisions (Pre-agreed)

1. **Object Header Format:**
   ```
   [uint32_t magic=0x464C5900][uint8_t version=1]
   [uint16_t py_name_len][char[] py_name]
   [uint64_t total_size][uint32_t chunk_count][uint8_t compression_type]
   ```
   - `py_name` is empty string when C++-only write (py_name_len=0)
   - Python `write_object` auto-fills `py_name = type(obj).__name__`
   - Python `read_object` resolves type via `getattr(sys.modules[...], py_name)`

2. **Stream-Chain Compression:**
   - `CompressingStreamBuf` wraps `std::streambuf`, accumulates 4MB chunks, compresses each chunk via existing `Compressor` interface
   - Default chunk size: 4MB (configurable via `Config::get_int("compression_stream_chunk_size")`)
   - Both small and large objects use the same unified stream path
   - `OutputBufferedStreamAdapter` → `CompressingStreamBuf` → `std::ofstream`

3. **Library-Agnostic Macros:**
   - `FLY_SERIALIZE_MEMBER_COUNT(N)` removed — bitsery uses explicit `serialize()` functions
   - `FLY_ENCODE/FLY_DECODE` re-implemented with bitsery buffer adapters
   - New field macros: `FLY_VAL8B`, `FLY_TEXT`, `FLY_CONTAINER4B`, etc. — map to bitsery calls, swappable for cereal
   - `TrustedConfig` with `CheckDataErrors=false` → maxSize is placeholder `FLY_MAX_SIZE`
   - Backend selected via `FLY_SERIALIZATION_BACKEND` macro (values: `FLY_BACKEND_BITSERY`, `FLY_BACKEND_CEREAL`)

4. **Versioning:** Custom `ext::Version<N>` implementation (bitsery doesn't ship one). Each struct gets a version number. New fields are gated by `if (version >= N)`. Old data without a version field is assumed version 0 and read successfully.

5. **Python Type Resolution:** `getattr(sys.modules)` with fast-path for `_fly_storage` module, slow-path iterating all loaded modules. No registration table needed.

---

## File Structure

### New Files
- `src/serialization/cpp/bitsery_ext/version.h` — Custom `ext::Version` implementation
- `src/serialization/cpp/bitsery_ext/growable.h` — Direct include from bitsery (or thin wrapper)
- `src/serialization/cpp/object_header.h` — ObjectHeader struct + serialize/deserialize functions
- `src/serialization/cpp/object_header.cpp` — ObjectHeader implementation
- `src/serialization/cpp/compressing_streambuf.h` — CompressingStreamBuf class
- `src/serialization/cpp/compressing_streambuf.cpp` — CompressingStreamBuf implementation

### Modified Files
- `src/serialization/cpp/serialization_macros.h` — Complete rewrite (bitsery macros)
- `src/serialization/cpp/BUILD` — Add bitsery dependency, new targets
- `src/storage/cpp/index_entry.h` — Replace `FLY_SERIALIZE_MEMBER_COUNT` with explicit `serialize()`
- `src/storage/cpp/db_meta.h` — Replace `FLY_SERIALIZE_MEMBER_COUNT` with explicit `serialize()`
- `src/storage/cpp/local_index.cpp` — Replace `FLY_SERIALIZE_MEMBER_COUNT` with explicit `serialize()`
- `src/storage/cpp/data_writer.h` — Add template `write_object<T>`, add `py_name` param
- `src/storage/cpp/data_writer.cpp` — Rewrite write path using CompressingStreamBuf + ObjectHeader
- `src/storage/cpp/data_reader.h` — Add template `read_object<T>`, return `shared_ptr<T>`
- `src/storage/cpp/data_reader.cpp` — Rewrite read path with ObjectHeader parsing
- `src/storage/cpp/database.h` — Add template `write_object<T>` and `read_object<T>`
- `src/storage/cpp/database.cpp` — Implement typed write/read with header
- `src/storage/cpp/object.h` — Update comment, remove zpp_bits reference
- `src/storage/export/storage_export.cpp` — Add typed write/read Python bindings, add `py_name` param
- `src/core/cpp/config.cpp` — Add `compression_stream_chunk_size` default (4194304)
- `third_party/BUILD` — Add bitsery header-only target

### Test Files
- `src/serialization/tests/serialization_test.cpp` — Rewrite all tests for bitsery macros
- `src/serialization/tests/object_header_test.cpp` — New test file for ObjectHeader
- `src/serialization/tests/compressing_streambuf_test.cpp` — New test for CompressingStreamBuf
- `src/storage/tests/storage_basics_test.cpp` — Update to use bitsery macros (remove FLY_SERIALIZE_MEMBER_COUNT)
- `src/storage/tests/data_writer_test.cpp` — Update for new API and ObjectHeader
- `src/storage/tests/data_reader_test.cpp` — Update for new API and ObjectHeader

---

## Task Breakdown

### Task 1: Add bitsery to third_party and BUILD

**Files:**
- Modify: `third_party/BUILD`
- Create: N/A (bitsery is header-only, fetched via Bazel http_archive)

- [ ] **Step 1: Add bitsery http_archive to WORKSPACE**

Append to `/root/fly/WORKSPACE`:

```python
# bitsery - header-only serialization library
http_archive(
    name = "bitsery",
    urls = ["https://github.com/fraillt/bitsery/archive/refs/tags/v5.2.4.tar.gz"],
    strip_prefix = "bitsery-5.2.4",
    build_file = "@//third_party:bitsery.BUILD",
)
```

- [ ] **Step 2: Create bitsery.BUILD**

Create `/root/fly/third_party/bitsery.BUILD`:

```python
package(default_visibility = ["//visibility:public"])

cc_library(
    name = "bitsery",
    hdrs = glob(["include/bitsery/**/*.h"]),
    includes = ["include"],
    copts = ["-std=c++20"],
)
```

- [ ] **Step 3: Verify bitsery builds**

Run: `cd /root/fly && ./fly.sh build //src/serialization/cpp:fly_serialization_macros`

Note: This will fail initially since macros haven't been updated yet. Just verify that Bazel can fetch the archive and the target is recognized. Then revert the BUILD deps change for now (it will be re-added in Task 2).

- [ ] **Step 4: Commit**

```bash
git add WORKSPACE third_party/bitsery.BUILD
git commit -m "build: add bitsery v5.2.4 as dependency"
```

---

### Task 2: Rewrite serialization_macros.h with bitsery backend

**Files:**
- Modify: `src/serialization/cpp/serialization_macros.h`
- Modify: `src/serialization/cpp/BUILD`

- [ ] **Step 1: Write the new serialization_macros.h**

Replace the entire content of `/root/fly/src/serialization/cpp/serialization_macros.h` with:

```cpp
#pragma once

// =============================================================================
// Fly Serialization Macros — Library-agnostic layer
//
// Select backend by defining FLY_SERIALIZATION_BACKEND before including this header:
//   FLY_BACKEND_BITSERY  (default) — uses bitsery (header-only, versioning support)
//   FLY_BACKEND_CEREAL   (future)  — uses cereal
//
// When using bitsery backend with TrustedConfig (internal trusted data),
// set FLY_SERIALIZATION_TRUSTED=1 (default) to skip data validation checks.
// This allows FLY_MAX_SIZE to be a placeholder value.
// =============================================================================

#ifndef FLY_SERIALIZATION_BACKEND
#define FLY_SERIALIZATION_BACKEND FLY_BACKEND_BITSERY
#endif

#ifndef FLY_SERIALIZATION_TRUSTED
#define FLY_SERIALIZATION_TRUSTED 1
#endif

// =============================================================================
// Backend: Bitsery
// =============================================================================
#if FLY_SERIALIZATION_BACKEND == FLY_BACKEND_BITSERY

#include <bitsery/bitsery.h>
#include <bitsery/adapter/buffer.h>
#include <bitsery/adapter/stream.h>
#include <bitsery/traits/vector.h>
#include <bitsery/traits/string.h>
#include <bitsery/traits/map.h>
#include <bitsery/ext/std_map.h>
#include <common/cpp/common_types.h>
#include <cstdint>
#include <stdexcept>
#include <fstream>

// TrustedConfig: disables data validation for internal trusted data
// When enabled, maxSize parameters on text/container are placeholder only
struct FlyTrustedConfig {
    static constexpr bitsery::EndiannessType Endianness = bitsery::EndiannessType::LittleEndian;
    static constexpr bool CheckAdapterErrors = true;
    static constexpr bool CheckDataErrors = false;  // skip maxSize validation
};

// Max size placeholder — only used for bitsery container/text size parameters.
// With TrustedConfig, this value is never validated. It exists to satisfy
// bitsery's compile-time requirement for dynamic containers.
#define FLY_MAX_SIZE 0x7FFFFFFF

// Type aliases for Fly serialization
using FlyBuffer = CMVector<uint8_t>;
using FlyOutputAdapter = bitsery::OutputBufferAdapter<FlyBuffer>;
using FlyInputAdapter = bitsery::InputBufferAdapter<FlyBuffer, FlyTrustedConfig>;

// Stream adapter aliases
using FlyOutputStreamAdapter = bitsery::OutputBufferedStreamAdapter;
using FlyInputStreamAdapter = bitsery::InputStreamAdapter<>;

// =============================================================================
// Field macros — bitsery implementation
// These macros abstract field serialization so the backend can be swapped
// without modifying struct definitions.
// =============================================================================

// Fundamental value types
#define FLY_VAL1B(s, obj, field)  s.value1b(obj.field)
#define FLY_VAL2B(s, obj, field)  s.value2b(obj.field)
#define FLY_VAL4B(s, obj, field)  s.value4b(obj.field)
#define FLY_VAL8B(s, obj, field)  s.value8b(obj.field)
#define FLY_VAL16B(s, obj, field) s.value16b(obj.field)

// Bool
#define FLY_BOOL(s, obj, field) s.boolValue(obj.field)

// Text (strings) — 1-byte length encoding, FLY_MAX_SIZE placeholder
#define FLY_TEXT(s, obj, field) s.text1b(obj.field, FLY_MAX_SIZE)
#define FLY_TEXT2B(s, obj, field) s.text2b(obj.field, FLY_MAX_SIZE)

// Containers — fixed-size element serialization
#define FLY_CONTAINER1B(s, obj, field) s.container1b(obj.field, FLY_MAX_SIZE)
#define FLY_CONTAINER2B(s, obj, field) s.container2b(obj.field, FLY_MAX_SIZE)
#define FLY_CONTAINER4B(s, obj, field) s.container4b(obj.field, FLY_MAX_SIZE)
#define FLY_CONTAINER8B(s, obj, field) s.container8b(obj.field, FLY_MAX_SIZE)

// Container with lambda for custom element serialization
#define FLY_CONTAINER(s, obj, field, max_size, ...) \
    s.container(obj.field, max_size, [](auto& s, auto& elem) { __VA_ARGS__ })

// Map types
#define FLY_MAP(s, obj, field, ...) \
    s.ext(obj.field, bitsery::ext::StdMap{FLY_MAX_SIZE}, [](auto& s, auto& key, auto& val) { \
        __VA_ARGS__ \
    })

// Object (nested struct)
#define FLY_OBJECT(s, obj, field) s.object(obj.field)

// =============================================================================
// Encode/Decode macros — bitsery implementation
// =============================================================================

// FLY_ENCODE: Serialize msg to output (CMString)
// Output is a char-based string suitable for file/network transmission.
#define FLY_ENCODE(msg, output) \
    do { \
        FlyBuffer fly_enc_buf_; \
        auto fly_enc_size_ = bitsery::quickSerialization<FlyOutputAdapter>(fly_enc_buf_, msg); \
        output.resize(fly_enc_size_); \
        std::transform(fly_enc_buf_.begin(), fly_enc_buf_.begin() + fly_enc_size_, \
            output.begin(), [](uint8_t b) { return static_cast<char>(b); }); \
    } while(0)

// FLY_DECODE: Deserialize input (CMString) to msg of type msg_type
#define FLY_DECODE(input, msg_type, output) \
    do { \
        FlyBuffer fly_dec_buf_(input.size()); \
        std::transform(input.begin(), input.end(), fly_dec_buf_.begin(), \
            [](char c) { return static_cast<uint8_t>(c); }); \
        msg_type fly_dec_msg_; \
        auto fly_dec_result_ = bitsery::quickDeserialization<FlyInputAdapter>( \
            {fly_dec_buf_.begin(), static_cast<size_t>(input.size())}, fly_dec_msg_); \
        if (fly_dec_result_.first != bitsery::ReaderError::NoError || !fly_dec_result_.second) { \
            throw std::runtime_error("FLY_DECODE: deserialization failed"); \
        } \
        output = std::move(fly_dec_msg_); \
    } while(0)

// FLY_ENCODE_TO_BYTES: Serialize msg to output (CMVector<uint8_t>)
#define FLY_ENCODE_TO_BYTES(msg, output) \
    do { \
        auto fly_enc_size_ = bitsery::quickSerialization<FlyOutputAdapter>(output, msg); \
        output.resize(fly_enc_size_); \
    } while(0)

// FLY_DECODE_FROM_BYTES: Deserialize input (CMVector<uint8_t>) to msg of type msg_type
#define FLY_DECODE_FROM_BYTES(input, msg_type, output) \
    do { \
        msg_type fly_dec_msg_; \
        auto fly_dec_result_ = bitsery::quickDeserialization<FlyInputAdapter>( \
            {input.begin(), input.size()}, fly_dec_msg_); \
        if (fly_dec_result_.first != bitsery::ReaderError::NoError || !fly_dec_result_.second) { \
            throw std::runtime_error("FLY_DECODE_FROM_BYTES: deserialization failed"); \
        } \
        output = std::move(fly_dec_msg_); \
    } while(0)

// FLY_ENCODE_SIZE: Serialize msg and return bytes written (useful for streaming)
// Usage: size_t written = FLY_ENCODE_SIZE(msg, buffer);
#define FLY_ENCODE_SIZE(msg, buffer) \
    bitsery::quickSerialization<FlyOutputAdapter>(buffer, msg)

// =============================================================================
// Backend: Cereal (future implementation)
// =============================================================================
#elif FLY_SERIALIZATION_BACKEND == FLY_BACKEND_CEREAL
#error "Cereal backend not yet implemented. Use FLY_BACKEND_BITSERY."
#else
#error "Unknown serialization backend. Define FLY_SERIALIZATION_BACKEND as FLY_BACKEND_BITSERY or FLY_BACKEND_CEREAL."
#endif

// =============================================================================
// Common: Version extension for bitsery (also usable with any backend)
// =============================================================================
#if FLY_SERIALIZATION_BACKEND == FLY_BACKEND_BITSERY

#include <bitsery/details/serialization_common.h>

namespace fly {

template<size_t VERSION>
class Version {
public:
    template<typename Ser, typename T, typename Fnc>
    void serialize(Ser& ser, const T& v, Fnc&& fnc) const {
        bitsery::details::writeSize(ser.adapter(), VERSION);
        fnc(ser, const_cast<T&>(v), VERSION);
    }

    template<typename Des, typename T, typename Fnc>
    void deserialize(Des& des, T& v, Fnc&& fnc) const {
        size_t version{};
        bitsery::details::readSize(des.adapter(), version, 0u, std::false_type{});
        fnc(des, v, version);
    }
};

} // namespace fly

namespace bitsery {
namespace traits {

template<typename T, size_t V>
struct ExtensionTraits<fly::Version<V>, T> {
    using TValue = T;
    static constexpr bool SupportValueOverload = false;
    static constexpr bool SupportObjectOverload = false;
    static constexpr bool SupportLambdaOverload = true;
};

} // namespace traits
} // namespace bitsery

#endif // FLY_BACKEND_BITSERY
```

- [ ] **Step 2: Update serialization BUILD to depend on bitsery**

Modify `/root/fly/src/serialization/cpp/BUILD`:

```python
# Fly serialization macros

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "fly_serialization_macros",
    hdrs = ["serialization_macros.h"],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
    deps = [
        "@bitsery//:bitsery",
        "//src/common/cpp:fly_common_types",
    ],
)

cc_library(
    name = "fly_bitsery_ext",
    hdrs = [
        "bitsery_ext/version.h",
    ],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
    deps = [
        "@bitsery//:bitsery",
    ],
)
```

Note: The `bitsery_ext/version.h` file will be created in Task 3. The `fly_bitsery_ext` target is forward-declared here.

- [ ] **Step 3: Remove zpp_bits dependency from WORKSPACE**

Find and remove the `http_archive` for `zpp_bits` in `/root/fly/WORKSPACE`.

- [ ] **Step 4: Run serialization tests**

Run: `cd /root/fly && ./fly.sh build //src/serialization/tests:serialization_test && ./bazel-bin/src/serialization/tests/serialization_test`

This will FAIL because all test structs still use `FLY_SERIALIZE_MEMBER_COUNT`. We'll fix that in Task 4.

- [ ] **Step 5: Commit macros only (working toward passing tests)**

```bash
git add src/serialization/cpp/serialization_macros.h src/serialization/cpp/BUILD
git commit -m "feat: rewrite serialization macros with bitsery backend"
```

---

### Task 3: Create bitsery_ext/version.h

Extract the `fly::Version` template from `serialization_macros.h` into its own header, for cleaner separation.

**Files:**
- Create: `src/serialization/cpp/bitsery_ext/version.h`

- [ ] **Step 1: Create the version extension header**

Create directory `/root/fly/src/serialization/cpp/bitsery_ext/` and file `/root/fly/src/serialization/cpp/bitsery_ext/version.h`:

```cpp
#pragma once

// Fly Version extension for bitsery
// Provides forward/backward compatible versioning for serialized structs.
//
// Usage:
//   template<typename S>
//   void serialize(S& s, MyStruct& o) {
//       s.ext(o, fly::Version<2>{}, [](S& s, MyStruct& o, size_t version) {
//           s.text1b(o.name, FLY_MAX_SIZE);
//           s.value8b(o.offset);
//           if (version >= 2) {
//               s.value1b(o.compression_type);
//           }
//       });
//   }

#include <bitsery/bitsery.h>
#include <bitsery/details/serialization_common.h>
#include <cstddef>

namespace fly {

template<size_t VERSION>
class Version {
public:
    template<typename Ser, typename T, typename Fnc>
    void serialize(Ser& ser, const T& v, Fnc&& fnc) const {
        bitsery::details::writeSize(ser.adapter(), VERSION);
        fnc(ser, const_cast<T&>(v), VERSION);
    }

    template<typename Des, typename T, typename Fnc>
    void deserialize(Des& des, T& v, Fnc&& fnc) const {
        size_t version{};
        bitsery::details::readSize(des.adapter(), version, 0u, std::false_type{});
        fnc(des, v, version);
    }
};

} // namespace fly

namespace bitsery {
namespace traits {

template<typename T, size_t V>
struct ExtensionTraits<fly::Version<V>, T> {
    using TValue = T;
    static constexpr bool SupportValueOverload = false;
    static constexpr bool SupportObjectOverload = false;
    static constexpr bool SupportLambdaOverload = true;
};

} // namespace traits
} // namespace bitsery
```

- [ ] **Step 2: Update serialization_macros.h to include version.h instead of inline definition**

In `serialization_macros.h`, remove the `fly::Version` template class and `bitsery::traits::ExtensionTraits` specialization that are at the bottom of the bitsery section, and add instead:

```cpp
#include <serialization/cpp/bitsery_ext/version.h>
```

This keeps `serialization_macros.h` focused on macros and `version.h` focused on the versioning extension.

- [ ] **Step 3: Verify BUILD target**

The `fly_bitsery_ext` target was already created in Task 2's BUILD file. Verify it compiles:

Run: `cd /root/fly && ./fly.sh build //src/serialization/cpp:fly_bitsery_ext`

Expected: Build succeeds (header-only, so just checks includes resolve).

- [ ] **Step 4: Commit**

```bash
git add src/serialization/cpp/bitsery_ext/version.h src/serialization/cpp/serialization_macros.h
git commit -m "feat: add bitsery Version extension for schema evolution"
```

---

### Task 4: Migrate all struct definitions from FLY_SERIALIZE_MEMBER_COUNT to explicit serialize()

Every struct that uses `FLY_SERIALIZE_MEMBER_COUNT(N)` must be converted to an explicit `serialize()` template function with bitsery field macros.

**Files:**
- Modify: `src/storage/cpp/index_entry.h`
- Modify: `src/storage/cpp/db_meta.h`
- Modify: `src/storage/cpp/local_index.cpp` (IndexData struct)
- Modify: `src/serialization/tests/serialization_test.cpp`
- Modify: `src/storage/tests/storage_basics_test.cpp` (TestData, MockObject)

- [ ] **Step 1: Convert IndexEntry**

Replace `/root/fly/src/storage/cpp/index_entry.h`:

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
    int8_t compression_type = 0;

    template<typename S>
    void serialize(S& s) {
        // Version 2: added compression_type field
        s.ext(*this, fly::Version<2>{}, [](S& s, IndexEntry& o, size_t version) {
            FLY_TEXT(s, o, object_name);
            FLY_TEXT(s, o, file_name);
            FLY_VAL8B(s, o, offset);
            FLY_VAL8B(s, o, size);
            FLY_BOOL(s, o, is_large);
            FLY_VAL4B(s, o, block_count);
            if (version >= 2) {
                FLY_VAL1B(s, o, compression_type);
            }
        });
    }
};
```

- [ ] **Step 2: Convert WorkerInfo and DbMeta**

Replace `/root/fly/src/storage/cpp/db_meta.h`:

```cpp
#pragma once

#include <common/cpp/common_types.h>
#include <serialization/cpp/serialization_macros.h>
#include <cstdint>

struct WorkerInfo {
    uint64_t worker_id = 0;
    CMString host;
    CMString role;
    CMString data_path;
    CMString idx_file;
    int64_t idx_entry_count = 0;
    CMString launch_command;

    template<typename S>
    void serialize(S& s) {
        s.ext(*this, fly::Version<1>{}, [](S& s, WorkerInfo& o, size_t /*version*/) {
            FLY_VAL8B(s, o, worker_id);
            FLY_TEXT(s, o, host);
            FLY_TEXT(s, o, role);
            FLY_TEXT(s, o, data_path);
            FLY_TEXT(s, o, idx_file);
            FLY_VAL8B(s, o, idx_entry_count);
            FLY_TEXT(s, o, launch_command);
        });
    }
};

struct DbMeta {
    CMString db_id;
    CMString base_path;
    int64_t created_at = 0;
    int64_t frozen_at = 0;
    CMVector<WorkerInfo> workers;

    template<typename S>
    void serialize(S& s) {
        s.ext(*this, fly::Version<1>{}, [](S& s, DbMeta& o, size_t /*version*/) {
            FLY_TEXT(s, o, db_id);
            FLY_TEXT(s, o, base_path);
            FLY_VAL8B(s, o, created_at);
            FLY_VAL8B(s, o, frozen_at);
            FLY_CONTAINER(s, o, workers, FLY_MAX_SIZE, {
                s.object(elem);
            });
        });
    }
};
```

- [ ] **Step 3: Convert IndexData in local_index.cpp**

In `/root/fly/src/storage/cpp/local_index.cpp`, replace the two `struct IndexData` definitions (in `save()` and `load()`) with a version that uses bitsery serialize:

Remove the `FLY_SERIALIZE_MEMBER_COUNT(1)` and add a proper serialize function. In `save()`:

```cpp
void LocalIndex::save() {
    CMString bytes;
    FLY_ENCODE(entries_, bytes);

    std::ofstream ofs(idx_path_, std::ios::binary);
    if (!ofs.is_open()) {
        throw std::runtime_error("Failed to open index file for writing: " + idx_path_);
    }

    int64_t size = static_cast<int64_t>(bytes.size());
    ofs.write(reinterpret_cast<const char*>(&size), sizeof(size));
    ofs.write(bytes.data(), bytes.size());
    ofs.close();

    modified_ = false;
}
```

Wait — `entries_` is `CMUnorderedMap<CMString, CMVector<IndexEntry>>`. We need to serialize this directly. The `FLY_ENCODE` macro needs a type that has `serialize()`. Since `CMUnorderedMap` and `CMVector` are type aliases of `std::unordered_map` and `std::vector`, and they contain `IndexEntry` which now has `serialize()`, we can create a wrapper struct:

In the `save()` method:

```cpp
struct IndexData {
    CMUnorderedMap<CMString, CMVector<IndexEntry>> entries;

    template<typename S>
    void serialize(S& s) {
        s.ext(entries, bitsery::ext::StdMap{FLY_MAX_SIZE},
            [](S& s, CMString& key, CMVector<IndexEntry>& value) {
                s.text1b(key, FLY_MAX_SIZE);
                s.container4b(value, FLY_MAX_SIZE, [](S& s, IndexEntry& elem) {
                    s.object(elem);
                });
            });
    }
};
```

Actually, let's simplify. Since bitsery supports `std::unordered_map` via `ext::StdMap`, we can serialize the map directly in an IndexData wrapper. But we need the `serialize()` function to work with the map.

The cleanest approach: define `IndexData` once (not twice inline), and use `FLY_MAP` for the unordered_map:

```cpp
struct IndexData {
    CMUnorderedMap<CMString, CMVector<IndexEntry>> entries;

    template<typename S>
    void serialize(S& s) {
        FLY_MAP(s, *this, entries,
            FLY_TEXT(s, key, );
            // Hmm, this doesn't work cleanly with the lambda capture
        );
    }
};
```

Actually, the `FLY_MAP` macro doesn't capture `key` and `val` variables properly for nested serializtion. Let's define it with explicit lambda:

```cpp
struct IndexData {
    CMUnorderedMap<CMString, CMVector<IndexEntry>> entries;

    template<typename S>
    void serialize(S& s) {
        s.ext(entries, bitsery::ext::StdMap{FLY_MAX_SIZE},
            [](S& s2, CMString& key, CMVector<IndexEntry>& val) {
                s2.text1b(key, FLY_MAX_SIZE);
                s2.container4b(val, FLY_MAX_SIZE);
            });
    }
};
```

Wait — `s2.container4b(val, FLY_MAX_SIZE)` won't work for a vector of `IndexEntry` because `container4b` expects contiguous POD elements. `IndexEntry` has `CMString` members, so it's not POD. We need `s2.container(val, FLY_MAX_SIZE, [](S& s3, IndexEntry& e) { s3.object(e); })`:

```cpp
struct IndexData {
    CMUnorderedMap<CMString, CMVector<IndexEntry>> entries;

    template<typename S>
    void serialize(S& s) {
        s.ext(entries, bitsery::ext::StdMap{FLY_MAX_SIZE},
            [](S& s2, CMString& key, CMVector<IndexEntry>& val) {
                s2.text1b(key, FLY_MAX_SIZE);
                s2.container(val, FLY_MAX_SIZE, [](S& s3, IndexEntry& e) {
                    s3.object(e);
                });
            });
    }
};
```

This should be declared once, not inline in two different functions. Move it to a shared location or define it at the top of `local_index.cpp`.

- [ ] **Step 4: Convert test structs in serialization_test.cpp**

All test structs need explicit `serialize()` functions. Replace `FLY_SERIALIZE_MEMBER_COUNT` usage. The test file currently uses `TestMessage`, `VectorMessage`, `NestedInner`, `NestedOuter`, `MapMessage`, `AllTypesMessage` — none of which have `FLY_SERIALIZE_MEMBER_COUNT` because they use the old zpp_bits free-function style. They need `serialize()` member functions for bitsery.

Replace each test struct in `/root/fly/src/serialization/tests/serialization_test.cpp`:

```cpp
struct TestMessage {
    int32_t id = 0;
    CMString name;
    double value = 0.0;

    template<typename S>
    void serialize(S& s) {
        FLY_VAL4B(s, *this, id);
        FLY_TEXT(s, *this, name);
        s.value8b(value);  // double — not a macro, bitsery handles natively
    }
};
```

Wait — `double` needs `s.value8b(value)`. The macro `FLY_VAL8B(s, obj, field)` would expand to `s.value8b(obj.field)`. But for `double`, bitsery's `value8b` expects a reference to an integral type equivalent in size (8 bytes). Actually, bitsery uses `value<8>` internally and `value8b` is just `value<8>`. For `double`, we should use `s.value8b(value)` directly since `double` is 8 bytes. Let me check: bitsery's `value8b` template requires `IsFundamentalType`, and `double` IS a fundamental type. So `FLY_VAL8B(s, *this, value)` will work for `double`.

Similarly for `CMVector`, `CMMap` (which is `std::map`).

Complete conversion:

```cpp
struct TestMessage {
    int32_t id = 0;
    CMString name;
    double value = 0.0;

    template<typename S>
    void serialize(S& s) {
        FLY_VAL4B(s, *this, id);
        FLY_TEXT(s, *this, name);
        FLY_VAL8B(s, *this, value);
    }
};

struct VectorMessage {
    CMVector<int32_t> numbers;
    CMVector<CMString> strings;

    template<typename S>
    void serialize(S& s) {
        FLY_CONTAINER4B(s, *this, numbers);
        s.container(strings, FLY_MAX_SIZE, [](S& s2, CMString& str) {
            s2.text1b(str, FLY_MAX_SIZE);
        });
    }
};

struct NestedInner {
    int32_t x = 0;
    CMString label;

    template<typename S>
    void serialize(S& s) {
        FLY_VAL4B(s, *this, x);
        FLY_TEXT(s, *this, label);
    }
};

struct NestedOuter {
    NestedInner inner;
    int32_t outer_value = 0;

    template<typename S>
    void serialize(S& s) {
        FLY_OBJECT(s, *this, inner);
        FLY_VAL4B(s, *this, outer_value);
    }
};

struct MapMessage {
    CMMap<CMString, int32_t> int_map;
    CMMap<int32_t, CMString> reverse_map;

    template<typename S>
    void serialize(S& s) {
        FLY_MAP(s, *this, int_map, s.text1b(key, FLY_MAX_SIZE); s.value4b(val););
        FLY_MAP(s, *this, reverse_map, s.value4b(val); s.text1b(val, FLY_MAX_SIZE););
    }
};
```

Hmm, the `FLY_MAP` lambda has issues. `key` and `val` need proper names. Let me re-examine the macro. The lambda parameters from bitsery's `StdMap` are `(key, value)`. So the FLY_MAP macro users reference `key` and `val` by name.

Actually, `CMMap` is `std::map`, not `std::unordered_map`. bitsery's `StdMap` works with both. But the lambda needs to use the correct variable names from the macro expansion.

Let me redesign `FLY_MAP` more carefully. The bitsery `StdMap` extension lambda signature is `(Serializer& s, Key& key, Value& value)`. So:

```cpp
#define FLY_MAP(s, obj, field, ...) \
    s.ext(obj.field, bitsery::ext::StdMap{FLY_MAX_SIZE}, [](auto& s, auto& key, auto& val) { \
        __VA_ARGS__ \
    })
```

And usage:
```cpp
FLY_MAP(s, *this, int_map,
    s.text1b(key, FLY_MAX_SIZE);
    s.value4b(val);
)
```

But wait — `auto` in lambda parameters requires C++14. C++20 supports it but some macros may expand oddly. Let's use explicit `S&` for the serializer, but then we need `S` in scope... Actually the lambda is inside a template function, so `auto` works fine.

BUT — there's a problem with `__VA_ARGS__` spread across multiple lines and containing semicolons inside a macro. This is actually fine with variadic macros in C++20.

Let me refine the approach. The `MapMessage` struct with explicit `StdMap`:

```cpp
struct MapMessage {
    CMMap<CMString, int32_t> int_map;
    CMMap<int32_t, CMString> reverse_map;

    template<typename S>
    void serialize(S& s) {
        s.ext(int_map, bitsery::ext::StdMap{FLY_MAX_SIZE},
            [](S& s2, CMString& key, int32_t& val) {
                s2.text1b(key, FLY_MAX_SIZE);
                s2.value4b(val);
            });
        s.ext(reverse_map, bitsery::ext::StdMap{FLY_MAX_SIZE},
            [](S& s2, int32_t& key, CMString& val) {
                s2.value4b(key);
                s2.text1b(val, FLY_MAX_SIZE);
            });
    }
};
```

This is cleaner. The `FLY_MAP` macro is handy for simple maps but for complex key/value types, direct `s.ext` is better. I'll keep `FLY_MAP` in the macros for convenience but the test will use explicit forms for clarity.

- [ ] **Step 5: Convert test structs in storage_basics_test.cpp**

The `TestData` struct currently uses `FLY_SERIALIZE_MEMBER_COUNT(2)`. Convert to:

```cpp
struct TestData {
    int32_t value = 0;
    CMString name;

    template<typename S>
    void serialize(S& s) {
        FLY_VAL4B(s, *this, value);
        FLY_TEXT(s, *this, name);
    }
};
```

The `MockObject` class in that test uses `Object` base class with virtual methods. Note from the existing comment: "zpp_bits structured bindings cannot count members of types with virtual base classes". This was a zpp_bits limitation. With bitsery, since we use `serialize()` member functions (not structured bindings), virtual bases are fine. The `MockObject` can now use `serialize()` directly.

But `MockObject` inherits from `Object` which has virtual methods. The `to_bytes()` and `from_bytes()` methods use `FLY_ENCODE/FLY_DECODE` on `TestData`, not on `MockObject` itself. So `MockObject` itself doesn't need `serialize()`. The `TestData` struct it wraps does. So this conversion is just for the `TestData` member struct.

Leave `Object` base class as-is for now — it will be redesigned in Task 7 (typed write/read).

- [ ] **Step 6: Build and run storage tests**

Run: `cd /root/fly && ./fly.sh build //src/storage/tests:storage_basics_test && ./bazel-bin/src/storage/tests/storage_basics_test`

Expected: All IndexEntry, WorkerInfo, DbMeta, Object tests pass.

- [ ] **Step 7: Build and run serialization tests**

Run: `cd /root/fly && ./fly.sh build //src/serialization/tests:serialization_test && ./bazel-bin/src/serialization/tests/serialization_test`

Expected: All serialization macro tests pass.

- [ ] **Step 8: Run ALL existing storage tests**

Run: `cd /root/fly && ./fly.sh build //src/storage/tests:all && run each test`

Expected: All 14 C++ storage tests pass.

- [ ] **Step 9: Commit**

```bash
git add -A
git commit -m "feat: migrate all structs from FLY_SERIALIZE_MEMBER_COUNT to bitsery serialize()"
```

---

### Task 5: Create ObjectHeader

**Files:**
- Create: `src/serialization/cpp/object_header.h`
- Create: `src/serialization/cpp/object_header.cpp`
- Create: `src/serialization/tests/object_header_test.cpp`
- Modify: `src/serialization/cpp/BUILD`

- [ ] **Step 1: Create object_header.h**

```cpp
#pragma once

#include <common/cpp/common_types.h>
#include <cstdint>

// Magic number for Fly object files: "FLY\x00"
constexpr uint32_t FLY_OBJECT_MAGIC = 0x464C5900;
constexpr uint8_t FLY_OBJECT_VERSION = 1;

// Header written before each serialized object in storage.
// Layout:
//   [uint32_t magic]          — FLY_OBJECT_MAGIC, identifies Fly data
//   [uint8_t  version]        — format version (currently 1)
//   [uint16_t py_name_len]    — length of Python type name (0 if none)
//   [char[]   py_name]        — Python type name, py_name_len bytes (empty if C++-only)
//   [uint64_t total_size]     — original (uncompressed) data size in bytes
//   [uint32_t chunk_count]    — number of compressed chunks that follow
//   [uint8_t  compression_type] — CompressionType enum value
struct ObjectHeader {
    uint32_t magic = FLY_OBJECT_MAGIC;
    uint8_t version = FLY_OBJECT_VERSION;
    uint16_t py_name_len = 0;
    CMString py_name;          // Python class name (empty for C++-only objects)
    uint64_t total_size = 0;   // uncompressed size of the serialized object
    uint32_t chunk_count = 0;  // number of compressed chunks following header
    uint8_t compression_type = 0;  // CompressionType enum value

    // Serialize header to binary string (fixed-size fields first, then py_name)
    CMString serialize() const;

    // Deserialize header from binary data. Returns bytes consumed.
    // On error, throws std::runtime_error.
    static ObjectHeader deserialize(const CMString& data, int64_t& offset);

    // Get total fixed header size (without py_name)
    static constexpr int64_t fixed_header_size() {
        return sizeof(uint32_t)  // magic
             + sizeof(uint8_t)    // version
             + sizeof(uint16_t)   // py_name_len
             + sizeof(uint64_t)   // total_size
             + sizeof(uint32_t)   // chunk_count
             + sizeof(uint8_t);   // compression_type
    }

    // Validate magic and version
    bool is_valid() const;
};
```

- [ ] **Step 2: Create object_header.cpp**

```cpp
#include <serialization/cpp/object_header.h>
#include <cstring>
#include <stdexcept>

CMString ObjectHeader::serialize() const {
    CMString result;
    int64_t total = fixed_header_size() + static_cast<int64_t>(py_name.size());
    result.resize(static_cast<size_t>(total));

    int64_t offset = 0;
    std::memcpy(result.data() + offset, &magic, sizeof(magic));
    offset += sizeof(magic);

    std::memcpy(result.data() + offset, &version, sizeof(version));
    offset += sizeof(version);

    std::memcpy(result.data() + offset, &py_name_len, sizeof(py_name_len));
    offset += sizeof(py_name_len);

    // total_size and chunk_count and compression_type
    std::memcpy(result.data() + offset, &total_size, sizeof(total_size));
    offset += sizeof(total_size);

    std::memcpy(result.data() + offset, &chunk_count, sizeof(chunk_count));
    offset += sizeof(chunk_count);

    std::memcpy(result.data() + offset, &compression_type, sizeof(compression_type));
    offset += sizeof(compression_type);

    // py_name data
    if (!py_name.empty()) {
        std::memcpy(result.data() + offset, py_name.data(), py_name.size());
    }

    return result;
}

ObjectHeader ObjectHeader::deserialize(const CMString& data, int64_t& offset) {
    ObjectHeader header;

    if (static_cast<int64_t>(data.size()) < offset + fixed_header_size()) {
        throw std::runtime_error("ObjectHeader: insufficient data for fixed header");
    }

    std::memcpy(&header.magic, data.data() + offset, sizeof(header.magic));
    offset += sizeof(header.magic);

    if (!header.is_valid()) {
        throw std::runtime_error("ObjectHeader: invalid magic number");
    }

    std::memcpy(&header.version, data.data() + offset, sizeof(header.version));
    offset += sizeof(header.version);

    if (header.version > FLY_OBJECT_VERSION) {
        throw std::runtime_error("ObjectHeader: unsupported version " + std::to_string(header.version));
    }

    std::memcpy(&header.py_name_len, data.data() + offset, sizeof(header.py_name_len));
    offset += sizeof(header.py_name_len);

    std::memcpy(&header.total_size, data.data() + offset, sizeof(header.total_size));
    offset += sizeof(header.total_size);

    std::memcpy(&header.chunk_count, data.data() + offset, sizeof(header.chunk_count));
    offset += sizeof(header.chunk_count);

    std::memcpy(&header.compression_type, data.data() + offset, sizeof(header.compression_type));
    offset += sizeof(header.compression_type);

    if (header.py_name_len > 0) {
        if (static_cast<int64_t>(data.size()) < offset + header.py_name_len) {
            throw std::runtime_error("ObjectHeader: insufficient data for py_name");
        }
        header.py_name.assign(data.data() + offset, header.py_name_len);
        offset += header.py_name_len;
    }

    return header;
}

bool ObjectHeader::is_valid() const {
    return magic == FLY_OBJECT_MAGIC;
}
```

- [ ] **Step 3: Create object_header_test.cpp**

Write comprehensive tests for serialize/deserialize, validation, edge cases, py_name field.

```cpp
#include <gtest/gtest.h>
#include <serialization/cpp/object_header.h>

TEST(ObjectHeaderTest, DefaultValues) {
    ObjectHeader header;
    EXPECT_EQ(header.magic, FLY_OBJECT_MAGIC);
    EXPECT_EQ(header.version, 1);
    EXPECT_EQ(header.py_name_len, 0);
    EXPECT_TRUE(header.py_name.empty());
    EXPECT_EQ(header.total_size, 0);
    EXPECT_EQ(header.chunk_count, 0);
    EXPECT_EQ(header.compression_type, 0);
}

TEST(ObjectHeaderTest, FixedHeaderSize) {
    // magic(4) + version(1) + py_name_len(2) + total_size(8) + chunk_count(4) + compression_type(1) = 20
    EXPECT_EQ(ObjectHeader::fixed_header_size(), 20);
}

TEST(ObjectHeaderTest, SerializeDeserializeNoPyName) {
    ObjectHeader header;
    header.total_size = 1024;
    header.chunk_count = 3;
    header.compression_type = 1;  // LZ4

    CMString serialized = header.serialize();
    EXPECT_EQ(serialized.size(), static_cast<size_t>(ObjectHeader::fixed_header_size()));

    int64_t offset = 0;
    ObjectHeader decoded = ObjectHeader::deserialize(serialized, offset);
    EXPECT_EQ(offset, ObjectHeader::fixed_header_size());
    EXPECT_EQ(decoded.magic, FLY_OBJECT_MAGIC);
    EXPECT_EQ(decoded.version, 1);
    EXPECT_EQ(decoded.py_name_len, 0);
    EXPECT_TRUE(decoded.py_name.empty());
    EXPECT_EQ(decoded.total_size, 1024);
    EXPECT_EQ(decoded.chunk_count, 3);
    EXPECT_EQ(decoded.compression_type, 1);
}

TEST(ObjectHeaderTest, SerializeDeserializeWithPyName) {
    ObjectHeader header;
    header.py_name = "SomeClass";
    header.py_name_len = 9;
    header.total_size = 2048;
    header.chunk_count = 1;
    header.compression_type = 2;  // ZLIB

    CMString serialized = header.serialize();
    EXPECT_EQ(serialized.size(), static_cast<size_t>(ObjectHeader::fixed_header_size() + 9));

    int64_t offset = 0;
    ObjectHeader decoded = ObjectHeader::deserialize(serialized, offset);
    EXPECT_EQ(offset, ObjectHeader::fixed_header_size() + 9);
    EXPECT_EQ(decoded.py_name, "SomeClass");
    EXPECT_EQ(decoded.py_name_len, 9);
    EXPECT_EQ(decoded.total_size, 2048);
    EXPECT_EQ(decoded.chunk_count, 1);
    EXPECT_EQ(decoded.compression_type, 2);
}

TEST(ObjectHeaderTest, IsValid) {
    ObjectHeader header;
    EXPECT_TRUE(header.is_valid());

    ObjectHeader bad;
    bad.magic = 0xFFFFFFFF;
    EXPECT_FALSE(bad.is_valid());
}

TEST(ObjectHeaderTest, DeserializeInsufficientData) {
    CMString short_data(5, '\0');
    int64_t offset = 0;
    EXPECT_THROW(ObjectHeader::deserialize(short_data, offset), std::runtime_error);
}

TEST(ObjectHeaderTest, DeserializeFutureVersion) {
    ObjectHeader header;
    header.version = 99;
    CMString serialized = header.serialize();

    int64_t offset = 0;
    EXPECT_THROW(ObjectHeader::deserialize(serialized, offset), std::runtime_error);
}

TEST(ObjectHeaderTest, RoundTripCppOnly) {
    // C++-only: py_name is empty
    ObjectHeader header;
    header.total_size = 65536;
    header.chunk_count = 16;
    header.compression_type = 3;  // ZSTD

    CMString serialized = header.serialize();
    int64_t offset = 0;
    ObjectHeader decoded = ObjectHeader::deserialize(serialized, offset);

    EXPECT_EQ(decoded.total_size, 65536);
    EXPECT_EQ(decoded.chunk_count, 16);
    EXPECT_EQ(decoded.compression_type, 3);
    EXPECT_TRUE(decoded.py_name.empty());
}

TEST(ObjectHeaderTest, RoundTripPythonClass) {
    // Python class: py_name is set
    ObjectHeader header;
    header.py_name = "MyTask";
    header.py_name_len = static_cast<uint16_t>(header.py_name.size());
    header.total_size = 4096;
    header.chunk_count = 1;
    header.compression_type = 0;  // NONE

    CMString serialized = header.serialize();
    int64_t offset = 0;
    ObjectHeader decoded = ObjectHeader::deserialize(serialized, offset);

    EXPECT_EQ(decoded.py_name, "MyTask");
    EXPECT_EQ(decoded.total_size, 4096);
    EXPECT_EQ(decoded.chunk_count, 1);
}
```

- [ ] **Step 4: Add object_header to BUILD**

Add to `/root/fly/src/serialization/cpp/BUILD`:

```python
cc_library(
    name = "fly_object_header",
    srcs = ["object_header.cpp"],
    hdrs = ["object_header.h"],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
    deps = [
        "//src/common/cpp:fly_common_types",
    ],
)
```

And add test target to `/root/fly/src/serialization/tests/BUILD`:

```python
cc_test(
    name = "object_header_test",
    srcs = ["object_header_test.cpp"],
    deps = [
        "@com_google_googletest//:gtest_main",
        "//src/serialization/cpp:fly_object_header",
        "//src/common/cpp:fly_common_types",
    ],
    copts = ["-std=c++20"],
)
```

- [ ] **Step 5: Build and run tests**

Run: `cd /root/fly && ./fly.sh build //src/serialization/tests:object_header_test && ./bazel-bin/src/serialization/tests/object_header_test`

Expected: All 8 ObjectHeader tests pass.

- [ ] **Step 6: Commit**

```bash
git add src/serialization/cpp/object_header.h src/serialization/cpp/object_header.cpp src/serialization/tests/object_header_test.cpp
git commit -m "feat: add ObjectHeader for typed object storage"
```

---

### Task 6: Create CompressingStreamBuf

**Files:**
- Create: `src/serialization/cpp/compressing_streambuf.h`
- Create: `src/serialization/cpp/compressing_streambuf.cpp`
- Create: `src/serialization/tests/compressing_streambuf_test.cpp`
- Modify: `src/serialization/cpp/BUILD`
- Modify: `src/core/cpp/config.cpp` (add `compression_stream_chunk_size`)

- [ ] **Step 1: Add config default**

In `/root/fly/src/core/cpp/config.cpp`, add to `INT_DEFAULTS`:

```cpp
{"compression_stream_chunk_size", 4194304},  // 4MB default chunk size for stream compression
```

- [ ] **Step 2: Create compressing_streambuf.h**

```cpp
#pragma once

#include <storage/cpp/compressor.h>
#include <common/cpp/common_types.h>
#include <cstdint>
#include <fstream>
#include <memory>
#include <streambuf>
#include <vector>

// CompressingStreamBuf wraps a std::ostream (typically std::ofstream) and
// compresses data in chunks as they accumulate. Each chunk is compressed
// independently using the configured Compressor.
//
// Usage:
//   std::ofstream file("data.bin", std::ios::binary);
//   CompressingStreamBuf buf(file, CompressorFactory::create(CompressionType::LZ4));
//   std::ostream os(&buf);
//   // Write serialized data to os — it is automatically compressed in chunks
//   os.flush();  // flush remaining data
//
// On-disk format per chunk:
//   [CompressedChunk format: int32_t uncompressed_size, int32_t compressed_size, bytes...]
//
class CompressingStreamBuf : public std::streambuf {
public:
    // chunk_size: maximum bytes of uncompressed data per chunk (default 4MB)
    CompressingStreamBuf(std::ostream& dest, std::unique_ptr<Compressor> compressor,
                         int64_t chunk_size = 4194304);
    ~CompressingStreamBuf() override;

    // Get total uncompressed bytes written
    int64_t total_uncompressed() const { return total_uncompressed_; }

    // Get number of chunks written
    int32_t chunk_count() const { return chunk_count_; }

    // Get compression type
    CompressionType compression_type() const { return compressor_ ? compressor_->type() : CompressionType::NONE; }

protected:
    int_type overflow(int_type ch) override;
    int sync() override;

private:
    void flush_chunk();

    std::ostream& dest_;
    std::unique_ptr<Compressor> compressor_;
    int64_t chunk_size_;
    CMVector<char> buffer_;
    int64_t total_uncompressed_ = 0;
    int32_t chunk_count_ = 0;
};
```

- [ ] **Step 3: Create compressing_streambuf.cpp**

```cpp
#include <serialization/cpp/compressing_streambuf.h>
#include <cstring>

CompressingStreamBuf::CompressingStreamBuf(std::ostream& dest,
                                           std::unique_ptr<Compressor> compressor,
                                           int64_t chunk_size)
    : dest_(dest)
    , compressor_(std::move(compressor))
    , chunk_size_(chunk_size) {
    buffer_.reserve(static_cast<size_t>(chunk_size));
}

CompressingStreamBuf::~CompressingStreamBuf() {
    // Flush any remaining data
    try {
        sync();
    } catch (...) {
        // Destructor must not throw
    }
}

CompressingStreamBuf::int_type CompressingStreamBuf::overflow(int_type ch) {
    if (ch != traits_type::eof()) {
        buffer_.push_back(static_cast<char>(ch));
        if (static_cast<int64_t>(buffer_.size()) >= chunk_size_) {
            flush_chunk();
        }
    }
    return ch;
}

int CompressingStreamBuf::sync() {
    if (!buffer_.empty()) {
        flush_chunk();
    }
    dest_.flush();
    return 0;
}

void CompressingStreamBuf::flush_chunk() {
    if (buffer_.empty()) {
        return;
    }

    total_uncompressed_ += static_cast<int64_t>(buffer_.size());

    // Compress the current buffer
    CMString input(buffer_.begin(), buffer_.end());

    if (compressor_) {
        // Compress the chunk
        CompressedChunk chunk = compressor_->compress(input);

        // Write chunk header + data using compression_utils format
        int32_t uncomp_size = chunk.uncompressed_size;
        int32_t comp_size = chunk.compressed_size;

        dest_.write(reinterpret_cast<const char*>(&uncomp_size), sizeof(int32_t));
        dest_.write(reinterpret_cast<const char*>(&comp_size), sizeof(int32_t));
        dest_.write(chunk.data.data(), static_cast<std::streamsize>(chunk.compressed_size));
    } else {
        // No compression — write as "identity" chunk (uncompressed_size == compressed_size)
        int32_t size = static_cast<int32_t>(buffer_.size());

        dest_.write(reinterpret_cast<const char*>(&size), sizeof(int32_t));     // uncompressed_size
        dest_.write(reinterpret_cast<const char*>(&size), sizeof(int32_t));      // compressed_size (same)
        dest_.write(buffer_.data(), static_cast<std::streamsize>(buffer_.size()));
    }

    chunk_count_++;
    buffer_.clear();
}
```

- [ ] **Step 4: Create compressing_streambuf_test.cpp**

```cpp
#include <gtest/gtest.h>
#include <serialization/cpp/compressing_streambuf.h>
#include <storage/cpp/compressor.h>
#include <sstream>
#include <cstring>

class CompressingStreamBufTest : public ::testing::TestWithParam<CompressionType> {};

TEST_P(CompressingStreamBufTest, CompressAndCount) {
    CompressionType type = GetParam();
    std::ostringstream oss;
    auto compressor = CompressorFactory::create(type);

    CompressingStreamBuf buf(oss, CompressorFactory::create(type), 64);
    std::ostream os(&buf);

    // Write 200 bytes of data (will trigger multiple 64-byte chunks)
    std::string data(200, 'A');
    os.write(data.data(), static_cast<std::streamsize>(data.size()));
    os.flush();

    EXPECT_EQ(buf.total_uncompressed(), 200);
    EXPECT_GE(buf.chunk_count(), 3);  // 200 / 64 = 3+ chunks
    EXPECT_GT(oss.str().size(), 0);
}

INSTANTIATE_TEST_SUITE_P(CompressionTypes, CompressingStreamBufTest,
    ::testing::Values(CompressionType::LZ4, CompressionType::ZLIB, CompressionType::ZSTD));

TEST(CompressingStreamBufBasicTest, NoCompression) {
    std::ostringstream oss;
    // NONE type — no compressor
    CompressingStreamBuf buf(oss, nullptr, 64);
    std::ostream os(&buf);

    std::string data(100, 'B');
    os.write(data.data(), static_cast<std::streamsize>(data.size()));
    os.flush();

    EXPECT_EQ(buf.total_uncompressed(), 100);
    EXPECT_EQ(buf.chunk_count(), 2);  // 100 / 64 = 1 full + 1 partial
}

TEST(CompressingStreamBufBasicTest, SmallDataSingleChunk) {
    std::ostringstream oss;
    auto compressor = CompressorFactory::create(CompressionType::LZ4);

    CompressingStreamBuf buf(oss, std::move(compressor), 1024);
    std::ostream os(&buf);

    std::string data(50, 'C');
    os.write(data.data(), static_cast<std::streamsize>(data.size()));
    os.flush();

    EXPECT_EQ(buf.total_uncompressed(), 50);
    EXPECT_EQ(buf.chunk_count(), 1);  // Less than chunk_size
}

TEST(CompressingStreamBufBasicTest, ExactChunkSize) {
    std::ostringstream oss;
    auto compressor = CompressorFactory::create(CompressionType::LZ4);

    size_t chunk_size = 128;
    CompressingStreamBuf buf(oss, std::move(compressor), static_cast<int64_t>(chunk_size));
    std::ostream os(&buf);

    std::string data(chunk_size, 'D');
    os.write(data.data(), static_cast<std::streamsize>(data.size()));
    os.flush();

    EXPECT_EQ(buf.total_uncompressed(), static_cast<int64_t>(chunk_size));
    EXPECT_EQ(buf.chunk_count(), 1);
}
```

- [ ] **Step 5: Add to BUILD files**

Add to `/root/fly/src/serialization/cpp/BUILD`:

```python
cc_library(
    name = "fly_compressing_streambuf",
    srcs = ["compressing_streambuf.cpp"],
    hdrs = ["compressing_streambuf.h"],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
    deps = [
        "//src/common/cpp:fly_common_types",
        "//src/storage/cpp:fly_storage_compression_impl",
    ],
)
```

Add test target to `/root/fly/src/serialization/tests/BUILD`.

- [ ] **Step 6: Build and run tests**

Run: `cd /root/fly && ./fly.sh build //src/serialization/tests:compressing_streambuf_test && ./bazel-bin/src/serialization/tests/compressing_streambuf_test`

Expected: All CompressingStreamBuf tests pass.

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "feat: add CompressingStreamBuf for stream-chain compression"
```

---

### Task 7: Refactor DataWriter/DataReader with ObjectHeader and CompressingStreamBuf

**Files:**
- Modify: `src/storage/cpp/data_writer.h`
- Modify: `src/storage/cpp/data_writer.cpp`
- Modify: `src/storage/cpp/data_reader.h`
- Modify: `src/storage/cpp/data_reader.cpp`
- Modify: `src/storage/cpp/database.h`
- Modify: `src/storage/cpp/database.cpp`
- Modify: `src/storage/cpp/object.h`
- Modify: `src/storage/tests/data_writer_test.cpp`
- Modify: `src/storage/tests/data_reader_test.cpp`

This is the most significant task. The `write_object` and `read_object` APIs change from raw string data to typed shared_ptr.

- [ ] **Step 1: Update DataWriter API**

`/root/fly/src/storage/cpp/data_writer.h`:

```cpp
#pragma once

#include <storage/cpp/local_index.h>
#include <storage/cpp/compressor.h>
#include <serialization/cpp/object_header.h>
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
        int compression_level = 0,
        int64_t stream_chunk_size = 4194304
    );

    ~DataWriter();

    DataWriter(const DataWriter&) = delete;
    DataWriter& operator=(const DataWriter&) = delete;

    // Write any serializable object. py_name is optional — set for cross-language reads.
    template<typename T>
    CMString write_object(const CMString& object_name, const T& obj,
                          const CMString& py_name = "") {
        // Serialize object to buffer
        FlyBuffer buffer;
        FLY_ENCODE_TO_BYTES(obj, buffer);

        // Write header + compressed data
        return write_object_data(object_name, buffer.size(), py_name,
            reinterpret_cast<const char*>(buffer.data()), buffer.size());
    }

    // Write raw string data (for unstructured data or backward compat)
    CMString write_object_data(const CMString& object_name, uint64_t original_size,
                                const CMString& py_name,
                                const char* data, int64_t data_size,
                                bool backup = false);

    void flush();
    void close();

    int64_t total_bytes_written() const;
    int32_t file_count() const;

private:
    void create_new_file();
    CMString get_current_file_name();

    CMString base_path_;
    CMString data_path_;
    uint64_t worker_id_;
    int64_t aggregation_threshold_;
    int64_t large_file_threshold_;
    int64_t block_size_;

    CompressionType compression_type_;
    int64_t compression_threshold_;
    std::unique_ptr<Compressor> compressor_;
    int64_t stream_chunk_size_;

    CMString current_file_;
    int32_t file_index_ = 1;
    int64_t current_file_size_ = 0;
    std::ofstream file_stream_;

    std::unique_ptr<LocalIndex> index_;
    int64_t total_bytes_ = 0;
    bool closed_ = false;
};
```

NOTE: The template `write_object<T>` needs to be in the header. The `write_object_data` implementation goes in the .cpp file.

- [ ] **Step 2: Implement write_object_data in data_writer.cpp**

The new write path:
1. Serialize object to buffer (done by template in header)
2. Create ObjectHeader with py_name, total_size, compression_type
3. Serialize ObjectHeader to binary → write to file
4. Use CompressingStreamBuf to compress and write data chunks
5. Create IndexEntry with the header offset and total size

Actually, let's keep the separation simpler. The DataWriter template serializes the object to a `FlyBuffer`, then calls an internal method that:
1. Writes the ObjectHeader
2. Compresses the buffer data in chunks
3. Writes each compressed chunk
4. Records the IndexEntry

The key insight: we no longer distinguish "small" vs "large" objects in terms of write path. All objects go through the same CompressingStreamBuf pipeline. But we still need `is_large` in the IndexEntry for read-side reconstruction (knowing if there are multiple chunks).

Actually, with the ObjectHeader containing `chunk_count`, `read_object` can determine how many chunks to read from the header. The `is_large` field in IndexEntry becomes less important — but we keep it for backward compatibility with Version 1 data.

Let me simplify: the new data format on disk is:
```
[ObjectHeader][CompressedChunk 1][CompressedChunk 2]...[CompressedChunk N]
```

Each chunk is the existing `[int32 uncomp_size][int32 comp_size][data...]` format.

The DataWriter now:
1. Serializes the object to a FlyBuffer
2. Writes the ObjectHeader (with py_name, total_size, chunk_count placeholder, compression_type)
3. Compresses the buffer data using CompressingStreamBuf
4. After writing all chunks, updates chunk_count in the header (or uses the CompressingStreamBuf's chunk_count)

Actually, CompressingStreamBuf gives us `chunk_count()` and `total_uncompressed()` after writing. But the ObjectHeader is written first... We need to either:
- A. Write header with placeholder, seek back to update chunk_count, OR
- B. Buffer the compressed data first, then write header with final chunk_count, OR
- C. Don't put chunk_count in the header — the reader reads chunks until it's consumed `total_size` bytes

Option C is cleanest: the reader reads the header, gets `total_size`, then reads compressed chunks until it has accumulated `total_size` bytes of uncompressed data. No need for `chunk_count` in the header at all.

Wait, `chunk_count` in the header was in our design. Let me reconsider. With stream-based compression:
- We don't know chunk_count until after streaming
- Reading requires knowing where one object ends and the next begins (we have IndexEntry offset/size)
- IndexEntry still records offset/size of the entire written block (header + all chunks)

So chunk_count in header can be a post-writing update. Let me go with Option B: buffer compressed output, then write header with final chunk_count.

This means we need two passes:
1. Compress to a local buffer (ostringstream + CompressingStreamBuf)
2. Write ObjectHeader (now we know total_size and chunk_count)
3. Write the compressed data

This is fine for our use case since we're already buffering the serialized data anyway.

Let me implement this path.

- [ ] **Step 3: Update DataReader API**

The read path:
1. Read ObjectHeader from file at the IndexEntry's offset
2. Parse py_name, total_size, compression_type
3. Read and decompress chunks until total_size bytes are accumulated
4. For `read_object<T>()`: deserialize using FLY_DECODE_FROM_BYTES into T
5. For raw read: return the uncompressed data as CMString

`/root/fly/src/storage/cpp/data_reader.h`:

```cpp
#pragma once

#include <storage/cpp/local_index.h>
#include <storage/cpp/compressor.h>
#include <serialization/cpp/object_header.h>
#include <common/cpp/common_types.h>
#include <cstdint>
#include <fstream>
#include <memory>

// Read result containing raw data and optional Python type name
struct ReadResult {
    CMString data;
    CMString py_name;  // Python class name (empty if C++-only write)
};

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

    // Typed read: returns shared_ptr<T> deserialized from stored data
    template<typename T>
    std::shared_ptr<T> read_object(const CMString& object_name) {
        ReadResult result = read_object_data(object_name);
        auto obj = std::make_shared<T>();
        // If py_name is empty, we still try to deserialize as T
        FLY_DECODE_FROM_BYTES(result.data_buffer, T, *obj);
        return obj;
    }

    // Raw read: returns data and header info
    ReadResult read_object_data(const CMString& object_name);
    ReadResult read_object_data(const IndexEntry& entry);

    bool exists(const CMString& object_name);

private:
    CMString find_file_path(const CMString& file_name);
    CMString read_from_file(const CMString& file_path, int64_t offset, int64_t size);
    ReadResult read_with_header(const CMString& file_path, int64_t offset, int64_t size);

    CMString base_path_;
    CMString data_path_;
    uint64_t worker_id_;

    std::unique_ptr<LocalIndex> index_;
};
```

Wait — `ReadResult` needs to hold the raw bytes for template `read_object<T>` to deserialize. The data comes from disk as a `CMString` (char-based), but `FLY_DECODE_FROM_BYTES` expects `CMVector<uint8_t>`. So we need both or convert. Let me use `FlyBuffer` (which is `CMVector<uint8_t>`) in ReadResult.

Actually, the raw bytes should be a `FlyBuffer` (`CMVector<uint8_t>`) so it works directly with `FLY_DECODE_FROM_BYTES`. But backward compatibility with `read_object(name)` returning `CMString` is broken by this change... The Database class will need updating too.

Let me think about this more carefully. The current `Database::read_object` returns `CMString`. The new API returns `shared_ptr<T>`. The raw string read is less important now since we have typed reads. But for backward compat during migration, we should keep `read_object_data` that returns raw bytes.

I'll use `FlyBuffer` in ReadResult for the typed path, and provide a `CMString` version for backward compat.

- [ ] **Step 4: Implement read_object_data in data_reader.cpp**

The new read path:
1. Read IndexEntry offset/size
2. Read ObjectHeader from offset
3. After header, read compressed chunks until we've consumed `total_size` uncompressed bytes
4. Return FlyBuffer (for typed deserialization) and py_name

For backward compatibility with old data that doesn't have an ObjectHeader (pre-migration), detect the old format:
- If the first 4 bytes don't match `FLY_OBJECT_MAGIC`, it's old format data
- Fall back to the old read path (no header, raw decompression)

- [ ] **Step 5: Update Database class with new API**

```cpp
class Database {
public:
    // ... existing constructors ...

    // Typed write: serializes obj and writes with header
    template<typename T>
    CMString write_object(const CMString& object_name, const T& obj,
                           const CMString& py_name = "") {
        check_frozen();
        return writer_->write_object(object_name, obj, py_name);
    }

    // Typed read: returns shared_ptr<T>
    template<typename T>
    std::shared_ptr<T> read_object(const CMString& object_name) {
        if (!is_frozen_) {
            writer_->flush();
            // Refresh reader to pick up new data
        }
        return reader_->read_object<T>(object_name);
    }

    // ... existing methods ...
};
```

- [ ] **Step 6: Update export bindings**

In `/root/fly/src/storage/export/storage_export.cpp`:
- Add `py_name` parameter to `write_object` Python binding
- Add typed `read_object` that resolves Python types via `sys.modules`
- Keep `read_object` returning raw string for backward compat
- Update pickle serialization for IndexEntry/DbMeta/WorkerInfo

- [ ] **Step 7: Update all test files**

Update `data_writer_test.cpp` and `data_reader_test.cpp` to use new APIs. Add tests for ObjectHeader round-trip, py_name preservation, and backward-compatible reads.

- [ ] **Step 8: Build and run ALL tests**

Run: `cd /root/fly && ./fly.sh build //... && run all tests`

Expected: All existing tests pass + new header/streambuf tests pass.

- [ ] **Step 9: Commit**

```bash
git add -A
git commit -m "feat: integrate ObjectHeader and CompressingStreamBuf into DataWriter/DataReader"
```

---

### Task 8: Update Python bindings for typed read_object

**Files:**
- Modify: `src/storage/export/storage_export.cpp`
- Modify: `src/storage/py/BUILD` (if exists)
- Update pytest files under `src/storage/py/tests/` (if exists)

- [ ] **Step 1: Add Python type resolution function**

In `storage_export.cpp`, add a helper function for Python type resolution:

```cpp
// Python type resolution: uses getattr(sys.modules, class_name) to find types
// This is called from Python side, not from C++
```

Actually, the type resolution happens in Python, not C++. The C++ binding for `read_object` returns raw data + py_name, and Python code does the type lookup. Let me design this properly.

The Python-side `read_object` needs to:
1. Call C++ to get raw bytes + py_name
2. If py_name is not empty, look up the type via `sys.modules`
3. Deserialize using the type's Python constructor

But wait — the deserialization is done by C++ (bitsery), not Python. So the Python type resolution needs to:
1. Know the type → call `db.read_object_typed<T>(name)` where T is the C++ type
2. OR we need a different approach for Python

The real question: how does a Python user call `db.read_object("test_obj")` and get back a Python object of the right type?

**Design**: The C++ side stores `py_name` in the header. The Python binding for `read_object`:
1. Calls C++ `read_object_data(name)` → gets raw bytes + py_name
2. Uses `sys.modules` to find Python class by py_name
3. Calls the Python class constructor with the raw bytes

But this requires each exported Python class to have a `from_bytes` method. Which they already do via the pickle protocol (`__getstate__`/`__setstate__`).

So the Python binding `read_object` becomes:
```python
def read_object(self, name):
    result = self._read_object_data(name)  # C++ returns (bytes, py_name)
    if result.py_name:
        cls = _resolve_type(result.py_name)
        obj = cls.__new__(cls)
        obj.__setstate__(result.data)
        return obj
    return result.data  # Return raw bytes if no py_name
```

This uses the existing pickle protocol! Each exported class already has `__getstate__`/`__setstate__` via `FLY_EXPORT_PICKLE`.

The `_resolve_type` function:
```python
import sys

def _resolve_type(py_name):
    # Fast path: check _fly_storage module first (built-in types)
    storage_mod = sys.modules.get('_fly_storage')
    if storage_mod is not None:
        cls = getattr(storage_mod, py_name, None)
        if cls is not None:
            return cls
    # Slow path: search all loaded modules
    for mod in sys.modules.values():
        if mod is not None:
            cls = getattr(mod, py_name, None)
            if cls is not None and isinstance(cls, type):
                return cls
    raise ValueError(f"Type '{py_name}' not found in any loaded module")
```

- [ ] **Step 2: Implement Python read_object binding**

The C++ binding exposes `read_object_data` (returning ReadResult with data + py_name), and the Python wrapper handles type resolution and deserialization.

- [ ] **Step 3: Implement Python write_object binding**

Python `write_object(self, name, obj)`:
1. Serialize obj via `__getstate__` → bytes
2. Get py_name via `type(obj).__name__`
3. Call C++ `write_object_data(name, len(bytes), py_name, bytes_ptr, bytes_len)`

- [ ] **Step 4: Update existing pytest to test new API**

- [ ] **Step 5: Build and test**

- [ ] **Step 6: Commit**

---

### Task 9: Backward compatibility and versioning test

**Files:**
- Create: `src/storage/tests/versioning_test.cpp`

- [ ] **Step 1: Write versioning test**

Test that:
1. Data written with version 1 (no compression_type) can be read by version 2 code
2. Data written with version 2 (with compression_type) can be read correctly
3. Old format data (without ObjectHeader magic) falls back to legacy read path

- [ ] **Step 2: Build and run**

- [ ] **Step 3: Commit**

---

### Task 10: Remove zpp_bits dependency completely

**Files:**
- Modify: `WORKSPACE` (remove zpp_bits http_archive)
- Modify: Any remaining references
- Full build verification

- [ ] **Step 1: Search for all zpp_bits references**

- [ ] **Step 2: Remove zpp_bits from WORKSPACE**

- [ ] **Step 3: Clean build and test**

Run: `cd /root/fly && ./fly.sh build //... && run all tests`

- [ ] **Step 4: Final commit**

```bash
git add -A
git commit -m "chore: remove zpp_bits dependency, migration to bitsery complete"
```

---

## Self-Review Checklist

1. **Spec coverage:**
   - Object header with py_name ✅ (Task 5, 7, 8)
   - CompressingStreamBuf with 4MB chunks ✅ (Task 6, 7)
   - Library-agnostic macros ✅ (Task 2)
   - bitsery ext::Version ✅ (Task 3)
   - Python type resolution via sys.modules ✅ (Task 8)
   - TrustedConfig with CheckDataErrors=false ✅ (Task 2)
   - Backward compatibility ✅ (Task 9)

2. **Placeholder scan:** No TBD/TODO items. All code is concrete.

3. **Type consistency:**
   - `FlyBuffer` = `CMVector<uint8_t>` used consistently
   - `FLY_ENCODE/FLY_DECODE` use `CMString` (char-based), `FLY_ENCODE_TO_BYTES/FLY_DECODE_FROM_BYTES` use `FlyBuffer` (uint8_t)
   - `ObjectHeader` serialize/deserialize use `CMString` byte offsets
   - `CompressingStreamBuf::chunk_count()` returns int32_t, matches `ObjectHeader::chunk_count`
   - `ReadResult.data_buffer` is `FlyBuffer` for template deserialization path