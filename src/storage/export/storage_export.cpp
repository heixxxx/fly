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
#include <storage/cpp/decompress_helper.h>
#include <storage/cpp/decompressing_streambuf.h>
#include <storage/cpp/fly_stream.h>
#include <common/cpp/write_context_hash.h>
#include <common/cpp/error_types.h>
#include <nanobind/operators.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/pair.h>
#include <istream>
#include <Python.h>

FLY_EXPORT_MODULE(_fly_storage) {

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
    FLY_EXPORT_READONLY_PROPERTY("total_uncompressed", &FlyStream::total_uncompressed)
    FLY_EXPORT_READONLY_PROPERTY("chunk_count", &FlyStream::chunk_count);

FLY_EXPORT_CLASS(Database, "EXStgDatabase")
    // Zero-copy write: access Python bytes directly without copying
    FLY_EXPORT_DEF("_write_pickle_bytes", [](Database& db, const CMString& name,
                                              nanobind::handle data,
                                              const CMString& py_name,
                                              bool backup) -> int {
        Py_buffer view;
        if (PyObject_GetBuffer(data.ptr(), &view, PyBUF_SIMPLE) < 0) {
            return -1;  // Error
        }
        auto result = static_cast<int>(db.write_pickle_bytes(name,
                                       static_cast<const char*>(view.buf),
                                       static_cast<int64_t>(view.len), py_name, backup));
        PyBuffer_Release(&view);
        return result;
    })
    FLY_EXPORT_DEF("_write_pickle_bytes", [](Database& db, const CMString& name,
                                              nanobind::handle data,
                                              const CMString& py_name) -> int {
        Py_buffer view;
        if (PyObject_GetBuffer(data.ptr(), &view, PyBUF_SIMPLE) < 0) {
            return -1;  // Error
        }
        auto result = static_cast<int>(db.write_pickle_bytes(name,
                                       static_cast<const char*>(view.buf),
                                       static_cast<int64_t>(view.len), py_name, false));
        PyBuffer_Release(&view);
        return result;
    })
    // 保存等级"none"（仅落盘不进 low 缓存）：数据搬运/merge 等场景用。
    FLY_EXPORT_DEF("_write_pickle_bytes", [](Database& db, const CMString& name,
                                              nanobind::handle data,
                                              const CMString& py_name,
                                              bool backup,
                                              bool populate_cache) -> int {
        Py_buffer view;
        if (PyObject_GetBuffer(data.ptr(), &view, PyBUF_SIMPLE) < 0) {
            return -1;  // Error
        }
        auto result = static_cast<int>(db.write_pickle_bytes(name,
                                       static_cast<const char*>(view.buf),
                                       static_cast<int64_t>(view.len), py_name,
                                       backup, populate_cache));
        PyBuffer_Release(&view);
        return result;
    })
    FLY_EXPORT_DEF("_read_streaming", [](Database& db, const CMString& name, bool backup) -> fly_export::tuple {
        auto [comp_data, py_name] = db.read_object_compressed(name, backup);
        return fly_export::make_tuple(
            fly_export::bytes(comp_data ? comp_data->data() : "", comp_data ? comp_data->size() : 0),
            py_name
        );
    })
    FLY_EXPORT_DEF("_read_streaming", [](Database& db, const CMString& name) -> fly_export::tuple {
        auto [comp_data, py_name] = db.read_object_compressed(name, false);
        return fly_export::make_tuple(
            fly_export::bytes(comp_data ? comp_data->data() : "", comp_data ? comp_data->size() : 0),
            py_name
        );
    })
    // Zero-copy read: decompress directly from FlyBuffer to Python bytes
    // Avoids intermediate CMString by decompressing directly into Python bytes object
    FLY_EXPORT_DEF("_read_decompressed", [](Database& db, const CMString& name, bool backup) -> fly_export::tuple {
        auto [comp_data, py_name] = db.read_object_compressed(name, backup);
        if (!comp_data || comp_data->empty()) {
            return fly_export::make_tuple(fly_export::bytes("", 0), py_name);
        }

        // Read expected decompressed size from ObjectHeader
        int64_t offset = 0;
        int64_t expected_size = 0;
        {
            ObjectHeader header;
            if (ObjectHeader::deserialize({comp_data->data(), comp_data->size()}, offset, header) &&
                header.total_size_ > 0) {
                expected_size = static_cast<int64_t>(header.total_size_);
            }
        }

        if (expected_size > 0) {
            // Create Python bytes object with exact size
            PyObject* py_bytes = PyBytes_FromStringAndSize(nullptr, expected_size);
            if (!py_bytes) {
                return fly_export::make_tuple(fly_export::bytes("", 0), py_name);
            }

            // Decompress directly into Python bytes buffer
            DecompressingStreamBuf dsbuf(comp_data->data(), comp_data->size());
            std::istream is(&dsbuf);
            is.read(PyBytes_AS_STRING(py_bytes), expected_size);
            auto gcount = is.gcount();

            if (gcount > 0 && gcount < expected_size) {
                _PyBytes_Resize(&py_bytes, gcount);
            }

            return fly_export::make_tuple(
                fly_export::bytes(py_bytes),
                py_name
            );
        } else {
            // Fallback: decompress to std::string then convert
            std::string result = fly::decompress_raw_data({comp_data->data(), comp_data->size()});
            return fly_export::make_tuple(
                fly_export::bytes(result.data(), result.size()),
                py_name
            );
        }
    })
    FLY_EXPORT_DEF("_read_decompressed", [](Database& db, const CMString& name) -> fly_export::tuple {
        auto [comp_data, py_name] = db.read_object_compressed(name, false);
        if (!comp_data || comp_data->empty()) {
            return fly_export::make_tuple(fly_export::bytes("", 0), py_name);
        }

        // Read expected decompressed size from ObjectHeader
        int64_t offset = 0;
        int64_t expected_size = 0;
        {
            ObjectHeader header;
            if (ObjectHeader::deserialize({comp_data->data(), comp_data->size()}, offset, header) &&
                header.total_size_ > 0) {
                expected_size = static_cast<int64_t>(header.total_size_);
            }
        }

        if (expected_size > 0) {
            // Create Python bytes object with exact size
            PyObject* py_bytes = PyBytes_FromStringAndSize(nullptr, expected_size);
            if (!py_bytes) {
                return fly_export::make_tuple(fly_export::bytes("", 0), py_name);
            }

            // Decompress directly into Python bytes buffer
            DecompressingStreamBuf dsbuf(comp_data->data(), comp_data->size());
            std::istream is(&dsbuf);
            is.read(PyBytes_AS_STRING(py_bytes), expected_size);
            auto gcount = is.gcount();

            if (gcount > 0 && gcount < expected_size) {
                _PyBytes_Resize(&py_bytes, gcount);
            }

            return fly_export::make_tuple(
                fly_export::bytes(py_bytes),
                py_name
            );
        } else {
            // Fallback: decompress to std::string then convert
            std::string result = fly::decompress_raw_data({comp_data->data(), comp_data->size()});
            return fly_export::make_tuple(
                fly_export::bytes(result.data(), result.size()),
                py_name
            );
        }
    })
    FLY_EXPORT_DEF("_get_py_name", [](Database& db, const CMString& name) -> CMString {
        return db.read_object_py_name(name);
    })
    FLY_EXPORT_DEF("_decompress_bytes", [](Database&, fly_export::bytes b) -> fly_export::bytes {
        CMString raw(b.c_str(), b.size());
        CMString result = fly::decompress_raw_data(raw);
        return fly_export::bytes(result.data(), result.size());
    })
    FLY_EXPORT_DEF("_write_temp_pickle", [](Database& db, const CMString& name,
                                             fly_export::bytes data, const CMString& py_name) {
        db.write_temp_pickle(name, data.c_str(), static_cast<int64_t>(data.size()), py_name);
    })
    FLY_EXPORT_DEF("_compress_pickle_bytes", [](Database& db, fly_export::bytes data,
                                                 const CMString& py_name) -> fly_export::bytes {
        CMString compressed = db.compress_pickle_bytes(data.c_str(),
                                                        static_cast<int64_t>(data.size()),
                                                        py_name);
        return fly_export::bytes(compressed.data(), compressed.size());
    })
    FLY_EXPORT_DEF("_commit_stream", [](Database& db, const CMString& name,
                                         FlyBufferPtr buf, const CMString& py_name,
                                         bool backup) -> int {
        return static_cast<int>(db.commit_stream(name, buf, py_name, backup));
    })
    FLY_EXPORT_DEF("_commit_stream", [](Database& db, const CMString& name,
                                         FlyBufferPtr buf, const CMString& py_name) -> int {
        return static_cast<int>(db.commit_stream(name, buf, py_name, false));
    })
    // 保存等级"none"（仅落盘不进 low 缓存）：数据搬运/merge 等场景用。
    FLY_EXPORT_DEF("_commit_stream", [](Database& db, const CMString& name,
                                         FlyBufferPtr buf, const CMString& py_name,
                                         bool backup, bool populate_cache) -> int {
        return static_cast<int>(db.commit_stream(name, buf, py_name, backup, populate_cache));
    })
    FLY_EXPORT_DEF("write_object_raw", [](Database& db, const CMString& name, const CMString& data, bool backup) -> int {
        return static_cast<int>(db.write_pickle_bytes(name, data.data(), static_cast<int64_t>(data.size()), "bytes", backup));
    })
    FLY_EXPORT_DEF("write_object_raw", [](Database& db, const CMString& name, const CMString& data) -> int {
        return static_cast<int>(db.write_pickle_bytes(name, data.data(), static_cast<int64_t>(data.size()), "bytes", false));
    })
    FLY_EXPORT_DEF("read_object_raw", [](Database& db, const CMString& name, bool backup) -> CMString {
        auto [comp_data, py_name] = db.read_object_compressed(name, backup);
        if (!comp_data || comp_data->empty()) return {};
        return fly::decompress_raw_data(CMString(comp_data->data(), comp_data->size()));
    })
    FLY_EXPORT_DEF("read_object_raw", [](Database& db, const CMString& name) -> CMString {
        auto [comp_data, py_name] = db.read_object_compressed(name, false);
        if (!comp_data || comp_data->empty()) return {};
        return fly::decompress_raw_data(CMString(comp_data->data(), comp_data->size()));
    })
    FLY_EXPORT_METHOD("backup_object", &Database::backup_object)
    FLY_EXPORT_METHOD("freeze", &Database::freeze)
    FLY_EXPORT_METHOD("is_frozen", &Database::is_frozen)
    FLY_EXPORT_METHOD("load_meta", &Database::load_meta)
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
    // All entries accept/return FlyBufferPtr directly (zero-copy in process):
    //   - Python objects: pickle.dumps → bytes → _set_var_bytes wraps into FlyBuffer
    //   - C++ exported objects: __getstate_buffer__ returns FlyBufferPtr → _set_var_buffer
    // get_var returns the FlyBufferPtr so the caller can __setstate_from_buffer__
    // a C++ object without copying through Python bytes.

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
        auto [found, result] = ds.try_read_remote(name);
        return fly_export::make_tuple(
            found,
            fly_export::bytes(result.data_buffer_.data(), result.data_buffer_.size()),
            result.py_name_,
            result.can_still_produce_);
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

// 静态读 _DB_META，不构造 Database（避免 merge_db 重复 register db_path）。
FLY_EXPORT_FUNCTION("ex_stg_load_meta_from_path",
    [](const CMString& db_path) -> DbMeta {
        return Database::load_meta_from_path(db_path);
    });

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

FLY_EXPORT_CLASS(fly::TempStore, "EXStgTempStore")
    FLY_EXPORT_DEF("put", [](fly::TempStore& self, const CMString& object_name,
                              fly_export::bytes data) {
        CMString compressed(data.c_str(), data.size());
        self.put(object_name, compressed);
    })
    FLY_EXPORT_DEF("get", [](fly::TempStore& self, const CMString& object_name) -> fly_export::tuple {
        auto [found, data] = self.get(object_name);
        if (!found) return fly_export::make_tuple(false, fly_export::bytes());
        return fly_export::make_tuple(true, fly_export::bytes(data.data(), data.size()));
    })
    FLY_EXPORT_METHOD("has", &fly::TempStore::has)
    FLY_EXPORT_METHOD("remove", &fly::TempStore::remove)
    FLY_EXPORT_METHOD("cleanup_all", &fly::TempStore::cleanup_all)
    FLY_EXPORT_METHOD("mem_bytes", &fly::TempStore::mem_bytes)
    FLY_EXPORT_METHOD("max_bytes", &fly::TempStore::max_bytes);

// ObjectCache diagnostics (test/observability).
FLY_EXPORT_FUNCTION("ex_stg_cache_high_size", []() -> size_t {
    return fly::ObjectCache::instance().high_size();
});
FLY_EXPORT_FUNCTION("ex_stg_cache_clear", []() {
    fly::ObjectCache::instance().clear();
});
// Hit statistics: returns (low_hits, low_misses, low_puts, low_evictions,
// high_hits, high_misses, high_puts, high_evictions).
FLY_EXPORT_FUNCTION("ex_stg_cache_stats", []() -> fly_export::tuple {
    const auto& s = fly::ObjectCache::instance().stats();
    return fly_export::make_tuple(
        s.low_hits.load(std::memory_order_relaxed),
        s.low_misses.load(std::memory_order_relaxed),
        s.low_puts.load(std::memory_order_relaxed),
        s.low_evictions.load(std::memory_order_relaxed),
        s.high_hits.load(std::memory_order_relaxed),
        s.high_misses.load(std::memory_order_relaxed),
        s.high_puts.load(std::memory_order_relaxed),
        s.high_evictions.load(std::memory_order_relaxed)
    );
});
}
