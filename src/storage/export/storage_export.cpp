#include <export/cpp/export_macros.h>
#include <serialization/cpp/serialization_macros.h>
#include <common/cpp/fly_buffer.h>
#include <serialization/cpp/object_header.h>
#include <storage/cpp/database.h>
#include <storage/cpp/storage_manager.h>
#include <storage/cpp/data_service.h>
#include <storage/cpp/object.h>
#include <storage/cpp/index_entry.h>
#include <storage/cpp/db_meta.h>
#include <storage/cpp/compressor.h>
#include <storage/cpp/object_cache.h>
#include <storage/cpp/memory_chunk_source.h>
#include <storage/cpp/decompressing_streambuf.h>
#include <storage/cpp/fly_stream.h>
#include <common/cpp/write_context_hash.h>
#include <common/cpp/error_types.h>
#include <log/cpp/logger.h>
#include <nanobind/operators.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/pair.h>
#include <istream>
#include <Python.h>

namespace {

// 抛 FATAL RuntimeError（[FATAL-DATA-CORRUPTION] 前缀，零容忍 §5）。
[[noreturn]] void throw_fatal_corruption(const CMString& name, const std::string& detail) {
    PyErr_SetString(PyExc_RuntimeError,
                    ("[FATAL-DATA-CORRUPTION] object '" + name + "': " + detail).c_str());
    throw fly_export::python_error();
}

}  // namespace

FLY_EXPORT_MODULE(_fly_storage) {

// L3 流式读（§8.1）：返回 FlyStream（读模式，file-protocol——pickle.Unpickler
// 增量消费）。先走 read_streaming（TIER1 Memory / TIER2 网络接收线程）；
// 失败回退 read_object_compressed（完整 TIER2 编排 + 零容忍）→ Memory 源。
// 消费端读完后必须查 stream.checksum_failed()（源校验状态，零容忍 §5）。
// 原生 m.def + take_ownership（nanobind 的 unique_ptr 返回 caster 对本类
// 不生效——裸指针 + 显式策略是完整支持路径）。
m.def("ex_stg_open_read_stream",
      ([](Database& db, const CMString& name, bool backup) -> FlyStream* {
    auto full = db.get_full_name(name);
    auto ds = fly::DataService::instance();
    auto r = ds->read_streaming(full);
    if (r.success && r.source && r.block_area_len > 0) {
        return new FlyStream(r.source, r.block_area_len);
    }
    if (r.rerr == fly::ReadError::CHECKSUM) {
        throw_fatal_corruption(name, "streaming source verify failed (local)");
    }
    if (r.rerr == fly::ReadError::OBJECT_NOT_FOUND ||
        r.rerr == fly::ReadError::DATA_NOT_READY) {
        // read_streaming 内部已完成副本轮换 + TIER3 刷新 + deadline（D4）——
        // 到这里 = 全源 miss，即真正"对象不可见"。#5 裁定禁止整缓冲回退
        //（原 NOT_FOUND 回退 read_object_compressed 已删，回退只会得到同样
        // 结果）。
        PyErr_SetString(PyExc_KeyError, ("Object '" + name + "' not found").c_str());
        throw fly_export::python_error();
    }
    if (r.error == "no streaming handler") {
        // 恒流式改造后 master/worker 均已注册 streaming handler——此分支
        // 不可达（防御性上抛，原 master 整缓冲主路径已随接线退役）。
        PyErr_SetString(PyExc_RuntimeError,
                        ("no streaming handler registered (process misconfig): '"
                         + name + "'").c_str());
        throw fly_export::python_error();
    }
    // 网络类轮换全败 / deadline 到期（read_streaming 已尽力：轮换+退避+
    // TIER3 刷新）——#5 裁定禁止整缓冲回退，如实上抛；消费端对象级
    // 重开（Python read_object 两轮）是上层兜底。
    PyErr_SetString(PyExc_RuntimeError,
                    ("streaming read failed for '" + name + "': " + r.error).c_str());
    throw fly_export::python_error();
      }),
      fly_export::rv_policy::take_ownership);

FLY_EXPORT_ENUM(CompressionType, "EXStgCompressionType")
    FLY_EXPORT_ENUM_VALUE("NONE", CompressionType::NONE)
    FLY_EXPORT_ENUM_VALUE("LZ4", CompressionType::LZ4)
    FLY_EXPORT_ENUM_VALUE("ZLIB", CompressionType::ZLIB)
    FLY_EXPORT_ENUM_VALUE("ZSTD", CompressionType::ZSTD);

FLY_EXPORT_FUNCTION("ex_stg_compression_type_from_name", [](const CMString& name) -> CompressionType {
    return CompressorFactory::type_from_name(name);
});

FLY_EXPORT_FUNCTION("ex_stg_compression_name_from_type", [](CompressionType type) -> CMString {
    return CompressorFactory::name_from_type(type);
});

FLY_EXPORT_CLASS(FlyBuffer, "FlyBuffer")
    FLY_EXPORT_INIT()
    FLY_EXPORT_DEF("write", [](FlyBuffer& buf, fly_export::bytes data) {
        buf.write(data.c_str(), data.size());
    })
    // read(n): file-protocol read for pickle.load(flybuffer). Advances a
    // read cursor; returns up to n bytes.
    FLY_EXPORT_DEF("read", [](FlyBuffer& buf, int64_t n) -> fly_export::bytes {
        CMString s = buf.read(n < 0 ? buf.size() - buf.pos() : static_cast<size_t>(n));
        return fly_export::bytes(s.data(), s.size());
    })
    // readline(): file-protocol readline for pickle.load(flybuffer).
    FLY_EXPORT_DEF("readline", [](FlyBuffer& buf) -> fly_export::bytes {
        CMString s = buf.readline();
        return fly_export::bytes(s.data(), s.size());
    })
    // readinto(bytearray): file-protocol readinto for pickle.load(flybuffer).
    // Writes directly into the caller's bytearray (pickle's working buffer) —
    // one serialization-inherent copy, no intermediate Python bytes object.
    // Returns the number of bytes written.
    FLY_EXPORT_DEF("readinto", [](FlyBuffer& buf, fly_export::object b) -> int64_t {
        PyObject* ba = b.ptr();
        if (!PyByteArray_Check(ba)) {
            throw fly_export::type_error("readinto() expects a bytearray");
        }
        Py_ssize_t cap = PyByteArray_Size(ba);
        char* dst = PyByteArray_AsString(ba);
        return static_cast<int64_t>(buf.readinto(dst, static_cast<size_t>(cap)));
    })
    // to_bytes(): copies the buffer into a Python bytes object. Needed for
    // pickle.loads (whose binary opcode stream is not line-oriented, so the
    // file-protocol read/readline is unsafe for generic pickle payloads).
    FLY_EXPORT_DEF("to_bytes", [](const FlyBuffer& buf) -> fly_export::bytes {
        return fly_export::bytes(buf.data(), buf.size());
    })
    FLY_EXPORT_DEF("seek", [](FlyBuffer& buf, int64_t p) { buf.seek(static_cast<size_t>(p)); })
    FLY_EXPORT_READONLY_PROPERTY("size", &FlyBuffer::size)
    FLY_EXPORT_READONLY_PROPERTY("pos", &FlyBuffer::pos);

// FlyStream
FLY_EXPORT_CLASS(FlyStream, "FlyStream")
    FLY_EXPORT_INIT(CompressionType, int64_t, const CMString&)
    FLY_EXPORT_INIT(CompressionType, int64_t)
    FLY_EXPORT_INIT(FlyBufferPtr)
    FLY_EXPORT_DEF("write", [](FlyStream& s, fly_export::bytes data) {
        s.write(data.c_str(), data.size());
    })
    FLY_EXPORT_DEF("flush", [](FlyStream& s) { s.flush(); })
    FLY_EXPORT_DEF("finish", [](FlyStream& s) -> FlyBufferPtr { return s.finish_write(); })
    FLY_EXPORT_DEF("read", [](FlyStream& s, int64_t n) -> fly_export::bytes {
        CMString d = (n < 0) ? s.read_all() : s.read(static_cast<size_t>(n));
        return fly_export::bytes(d.data(), d.size());
    })
    FLY_EXPORT_DEF("readline", [](FlyStream& s) -> fly_export::bytes {
        CMString d = s.readline();
        return fly_export::bytes(d.data(), d.size());
    })
    FLY_EXPORT_DEF("readinto", [](FlyStream& s, fly_export::object b) -> int64_t {
        Py_buffer view;
        if (PyObject_GetBuffer(b.ptr(), &view, PyBUF_WRITABLE) < 0) {
            PyErr_Clear();
            if (PyObject_GetBuffer(b.ptr(), &view, PyBUF_SIMPLE) < 0)
                throw fly_export::type_error("readinto() expects a buffer");
        }
        auto n = static_cast<int64_t>(s.readinto(static_cast<char*>(view.buf), static_cast<size_t>(view.len)));
        PyBuffer_Release(&view);
        return n;
    })
    FLY_EXPORT_DEF("writable", [](const FlyStream& s) { return s.is_write_mode(); })
    FLY_EXPORT_DEF("readable", [](const FlyStream& s) { return !s.is_write_mode(); })
    FLY_EXPORT_DEF("checksum_failed", [](const FlyStream& s) { return s.checksum_failed(); })
    FLY_EXPORT_DEF("finish_and_commit", [](FlyStream& s, bool backup,
                                           bool populate_cache) -> int64_t {
        return s.finish_and_commit(backup, populate_cache);
    })
    FLY_EXPORT_READONLY_PROPERTY("total_uncompressed", &FlyStream::total_uncompressed)
    FLY_EXPORT_READONLY_PROPERTY("chunk_count", &FlyStream::chunk_count)
    // 读模式源元数据（open 返回即有效）——read_object 单拉分流。
    FLY_EXPORT_READONLY_PROPERTY("py_name", &FlyStream::py_name)
    // temp 标记（缓存双池路由）：源携带（本地判定/META 告知）。
    FLY_EXPORT_READONLY_PROPERTY("is_temp", &FlyStream::stream_is_temp);

FLY_EXPORT_CLASS(Database, "EXStgDatabase")
    // L1 大对象流式写（§9.1）：open → pickle.dump(stream) →
    // stream.finish_and_commit()。frozen 时返回 None（裸指针 +
    // take_ownership——nanobind 无 holder 概念，与读侧工厂同路径）。
    // temp=true（T2d 2026-08-31）：temp 写流式化 sink（temp_writer_ 增量
    // 直写，取代 _write_temp_pickle 整对象缓冲路径）。
    .def("open_write_stream",
         [](Database& db, const CMString& name, const CMString& py_name, bool temp) -> FlyStream* {
             return db.open_write_stream(name, py_name, temp);
         },
         nanobind::arg("name"), nanobind::arg("py_name"), nanobind::arg("temp") = false,
         fly_export::rv_policy::take_ownership)
    // _write_pickle_bytes / _read_streaming / _read_decompressed / _is_temp /
    // _decompress_bytes / _compress_pickle_bytes 已删除（T2b 2026-08-31，
    // 生产零使用——恒流式改造后写侧统一 open_write_stream→finish_and_commit，
    // 读侧统一 ex_stg_open_read_stream；C++ 侧 write_pickle_bytes /
    // compress_pickle_bytes / read_object_compressed 方法保留——单测与
    // ObjectCache populate/low-tier 内部路径仍用）。
    // _write_temp_pickle 已删除（T2d 2026-08-31 temp 写流式化：temp 路径统一
    // open_write_stream(temp=true) → finish_and_commit 增量直写）。
    // _commit_stream 已删除（T2c 2026-08-31 写侧恒流式：非流式分支随
    // streaming_write_threshold 开关一并退役——open_write_stream →
    // finish_and_commit 是唯一写路径，C++ Database::commit_stream 无调用者
    // 同步删除）。
    // write_object_raw / read_object_raw / _write_pickle_bytes 已删除
    // （2026-08-30 / T2b 2026-08-31 用户裁定，生产零使用；写侧统一
    // open_write_stream → finish_and_commit 恒流式路径）。
    FLY_EXPORT_METHOD("backup_object", &Database::backup_object)
    FLY_EXPORT_METHOD("freeze", &Database::freeze)
    FLY_EXPORT_METHOD("is_frozen", &Database::is_frozen)
    FLY_EXPORT_METHOD("get_db_path", &Database::get_db_path)
    FLY_EXPORT_METHOD("get_data_path", &Database::get_data_path)
    FLY_EXPORT_METHOD("get_full_name", &Database::get_full_name)
    FLY_EXPORT_METHOD("get_writer_id", &Database::get_writer_id)
    FLY_EXPORT_METHOD("reset", &Database::reset)
    FLY_EXPORT_METHOD("remove_object", &Database::remove_object)
    FLY_EXPORT_DEF("_put_temp_data", [](Database& db, const CMString& name, fly_export::bytes compressed_data) {
        auto buf = CMMakeShared<FlyBuffer>();
        buf->write(compressed_data.c_str(), compressed_data.size());
        db.put_temp_data(name, buf);
    })
    // ---- Var service ----
    // All entries accept/return FlyBufferPtr directly (zero-copy in process).
    // var 是 Python 业务侧轻量对象 API（pickle 协议）；C++ 导出对象不经 var
    //（2026-09-02 裁定）。type_name 仅作信息性元数据。

    // _set_var_bytes(name, pickle_bytes, type_name) -> bool (success).
    // For Python objects serialized via pickle.
    FLY_EXPORT_DEF("_set_var_bytes", [](Database& db, const CMString& name,
                                         CMString value, const CMString& type_name) -> bool {
        // nanobind constructs `value` from Python bytes (1 boundary copy, inherent).
        // take() moves it into the FlyBuffer — no second copy.
        auto buf = CMMakeShared<FlyBuffer>();
        buf->take(std::move(value));
        return db.set_var(name, buf, type_name);
    })
    // _set_var_buffer(name, FlyBufferPtr, type_name) -> bool (success).
    // For C++ exported objects (zero-copy: the shared FlyBufferPtr goes straight in).
    FLY_EXPORT_DEF("_set_var_buffer", [](Database& db, const CMString& name,
                                          CMSharedPtr<FlyBuffer> value, const CMString& type_name) -> bool {
        return db.set_var(name, value, type_name);
    })
    // _get_var(name) -> (success: bool, value: FlyBufferPtr, type_name: str).
    // success=false (value=nullptr) means the var does not exist; no bytes are
    // constructed in that case.
    FLY_EXPORT_DEF("_get_var", [](Database& db, const CMString& name) -> fly_export::tuple {
        auto [ok, buf, type_name] = db.get_var(name);
        return fly_export::make_tuple(ok, ok ? buf : nullptr, type_name);
    })
    FLY_EXPORT_DEF("_remove_var", [](Database& db, const CMString& name) {
        db.remove_var(name);
    })
    // _inject_var(name, FlyBufferPtr, type_name): worker-side local cache fill
    // from TaskAssignMessage var_payloads (no network).
    FLY_EXPORT_DEF("_inject_var", [](Database& db, const CMString& name,
                                      CMSharedPtr<FlyBuffer> value, const CMString& type_name) {
        db.inject_var(name, value, type_name);
    })
    FLY_EXPORT_DEF("_drop_local_var", [](Database& db, const CMString& name) {
        db.drop_local_var(name);
    });

FLY_EXPORT_CLASS(fly::DataService, "EXStgDataService")
    FLY_EXPORT_DEF("try_read_local", [](fly::DataService& ds, const CMString& name) -> fly_export::tuple {
        auto [found, result] = ds.try_read_local(name);
        return fly_export::make_tuple(
            found,
            fly_export::bytes(
                result.data_buffer_.data(),
                result.data_buffer_.size()),
            result.py_name_
        );
    })
    FLY_EXPORT_DEF("lookup_remote_idx", [](fly::DataService& ds, const CMString& name) -> fly_export::tuple {
        auto info = ds.lookup_remote_idx(name);
        bool has = (info.worker_id_ != 0 || !info.host_.empty());
        return fly_export::make_tuple(has, info.worker_id_, info.host_, info.port_);
    })
    FLY_EXPORT_DEF("update_remote_idx", [](fly::DataService& ds, const CMString& name,
                                            uint64_t worker_id, const CMString& host, int32_t port) {
        ds.update_remote_idx(name, worker_id, host, port);
    })
    FLY_EXPORT_DEF("has_local_object", [](fly::DataService& ds, const CMString& name) -> bool {
        return ds.has_local_object(name);
    })
    FLY_EXPORT_DEF("has_remote_location", [](fly::DataService& ds, const CMString& name) -> bool {
        return ds.has_remote_location(name);
    })
    FLY_EXPORT_DEF("get_remote_workers", [](fly::DataService& ds, const CMString& name) -> CMVector<uint64_t> {
        return ds.get_remote_workers(name);
    })
    FLY_EXPORT_DEF("try_read_remote", [](fly::DataService& ds, const CMString& name) -> fly_export::tuple {
        try {
            auto [found, result] = ds.try_read_remote(name);
            return fly_export::make_tuple(
                found,
                fly_export::bytes(result.data_buffer_.data(), result.data_buffer_.size()),
                result.py_name_,
                result.can_still_produce_);
        } catch (const fly::DataCorruptionError& e) {
            throw_fatal_corruption(name, e.what());
        }
    })
    FLY_EXPORT_METHOD("drain_write_back", &fly::DataService::drain_write_back)
    FLY_EXPORT_METHOD("stop_write_back", &fly::DataService::stop_write_back)
    FLY_EXPORT_METHOD("has_database", &fly::DataService::has_database);

FLY_EXPORT_FUNCTION("ex_stg_get_data_service", []() { return fly::DataService::instance(); });

FLY_EXPORT_CLASS(StorageManager, "EXStgStorageManager")
    FLY_EXPORT_METHOD("close_all", &StorageManager::close_all)
    FLY_EXPORT_METHOD("reset", &StorageManager::reset)
    FLY_EXPORT_DEF("get_or_create_database", [](StorageManager& sm, const CMString& db_path) -> CMSharedPtr<Database> {
        return sm.get_or_create_database(db_path);
    });

FLY_EXPORT_CLASS(IndexEntry, "EXStgIndexEntry")
    FLY_EXPORT_INIT()
    FLY_EXPORT_INIT(CMString, CMString, int64_t, int64_t, bool, int32_t)
    FLY_EXPORT_READONLY_ATTR("object_name", &IndexEntry::object_name_)
    FLY_EXPORT_READONLY_ATTR("file_name", &IndexEntry::file_name_)
    FLY_EXPORT_READONLY_ATTR("offset", &IndexEntry::offset_)
    FLY_EXPORT_READONLY_ATTR("size", &IndexEntry::size_)
    FLY_EXPORT_READONLY_ATTR("is_large", &IndexEntry::is_large_)
    FLY_EXPORT_READONLY_ATTR("block_count", &IndexEntry::block_count_)
    FLY_EXPORT_SERIALIZE(IndexEntry);

FLY_EXPORT_CLASS(DbMeta, "EXStgDbMeta")
    FLY_EXPORT_INIT()
    FLY_EXPORT_INIT(int64_t)
    FLY_EXPORT_READONLY_ATTR("created_at", &DbMeta::created_at_)
    FLY_EXPORT_ATTR("workers", &DbMeta::workers_)
    FLY_EXPORT_SERIALIZE(DbMeta);

FLY_EXPORT_CLASS(WorkerInfo, "EXStgWorkerInfo")
    FLY_EXPORT_INIT()
    FLY_EXPORT_INIT(uint64_t, CMString, CMString, CMString, CMString)
    FLY_EXPORT_READONLY_ATTR("worker_id", &WorkerInfo::worker_id_)
    FLY_EXPORT_READONLY_ATTR("writer_id", &WorkerInfo::writer_id_)
    FLY_EXPORT_READONLY_ATTR("hostname", &WorkerInfo::hostname_)
    FLY_EXPORT_READONLY_ATTR("ip_address", &WorkerInfo::ip_address_)
    FLY_EXPORT_READONLY_ATTR("launch_command", &WorkerInfo::launch_command_)
    FLY_EXPORT_SERIALIZE(WorkerInfo);

FLY_EXPORT_FUNCTION("ex_stg_get_storage_manager", []() { return StorageManager::instance(); });

FLY_EXPORT_FUNCTION("ex_stg_create_database", [](const CMString& db_path, const CMString& data_path, uint64_t writer_id) -> CMSharedPtr<Database> {
    return CMMakeShared<Database>(db_path, data_path, writer_id, "", "");
});

FLY_EXPORT_FUNCTION("ex_stg_create_database_with_path", [](const CMString& db_path, const CMString& data_path, uint64_t writer_id, const CMString& /*existing_db_path*/) -> CMSharedPtr<Database> {
    // db_path 参数保留仅为签名兼容（== db_path，existing_db_path 已废弃忽略）。
    // 等价于 ex_stg_create_database。
    return CMMakeShared<Database>(db_path, data_path, writer_id);
});

// _DB_META（JSON）读取在 Python 编排层（storage/py/db_meta.py 的
// DbMetaFile + Database.load_meta_from_path），C++ 不再提供静态读。

FLY_EXPORT_FUNCTION("ex_stg_compute_write_context_hash",
    [](const CMString& task_name, const CMString& task_module,
       const CMVector<CMString>& args, const CMVector<CMString>& inputs) -> CMString {
        return compute_write_context_hash(task_name, task_module, args, inputs);
    });

FLY_EXPORT_ENUM(fly::TaskErrorType, "EXStgErrorType")
    FLY_EXPORT_ENUM_VALUE("UNKNOWN", fly::TaskErrorType::UNKNOWN)
    FLY_EXPORT_ENUM_VALUE("EXECUTION_ERROR", fly::TaskErrorType::EXECUTION_ERROR)
    FLY_EXPORT_ENUM_VALUE("WRITE_TO_FROZEN_DB", fly::TaskErrorType::WRITE_TO_FROZEN_DB)
    FLY_EXPORT_ENUM_VALUE("WRITE_REGISTRATION_FAILED", fly::TaskErrorType::WRITE_REGISTRATION_FAILED)
    FLY_EXPORT_ENUM_VALUE("WRITE_REGISTRATION_TIMEOUT", fly::TaskErrorType::WRITE_REGISTRATION_TIMEOUT)
    FLY_EXPORT_ENUM_VALUE("WRITE_PROVENANCE_MISMATCH", fly::TaskErrorType::WRITE_PROVENANCE_MISMATCH)
    FLY_EXPORT_ENUM_VALUE("WRITE_DUPLICATE_SKIPPED", fly::TaskErrorType::WRITE_DUPLICATE_SKIPPED);

FLY_EXPORT_ENUM(fly::WriteErrorType, "EXStgWriteErrorType")
    FLY_EXPORT_ENUM_VALUE("OK", fly::WriteErrorType::OK)
    FLY_EXPORT_ENUM_VALUE("FROZEN_DB", fly::WriteErrorType::FROZEN_DB)
    FLY_EXPORT_ENUM_VALUE("REGISTRATION_FAILED", fly::WriteErrorType::REGISTRATION_FAILED)
    FLY_EXPORT_ENUM_VALUE("REGISTRATION_TIMEOUT", fly::WriteErrorType::REGISTRATION_TIMEOUT)
    FLY_EXPORT_ENUM_VALUE("DUPLICATE_SKIPPED", fly::WriteErrorType::DUPLICATE_SKIPPED);

FLY_EXPORT_FUNCTION("ex_stg_get_last_error_type", []() -> int {
    return static_cast<int>(fly::WorkerAgentContext::get_last_error_type());
});

// EXStgTempStore export 已删除（2026-08-30 去"①形态"裁定：temp 内存 LRU/
// eviction 机制整体退役——temp 压缩 record 恒在盘上）。

// ObjectCache diagnostics (test/observability).
FLY_EXPORT_FUNCTION("ex_stg_cache_high_size", []() -> size_t {
    return fly::ObjectCache::instance().high_size();
});
FLY_EXPORT_FUNCTION("ex_stg_cache_clear", []() {
    fly::ObjectCache::instance().clear();
});
// Hit statistics: returns (high_hits, high_misses, high_puts, high_evictions).
//（low_* 四元组已随 T4 2026-08-31 low_ 池删除；消费方为 QA 缓存观测脚本，
//  已同步改为 4 元组。）
FLY_EXPORT_FUNCTION("ex_stg_cache_stats", []() -> fly_export::tuple {
    const auto& s = fly::ObjectCache::instance().stats();
    return fly_export::make_tuple(
        s.high_hits.load(std::memory_order_relaxed),
        s.high_misses.load(std::memory_order_relaxed),
        s.high_puts.load(std::memory_order_relaxed),
        s.high_evictions.load(std::memory_order_relaxed)
    );
});
}
