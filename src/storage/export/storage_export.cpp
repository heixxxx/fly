#include <export/cpp/export_macros.h>
#include <serialization/cpp/serialization_macros.h>
#include <serialization/cpp/fly_buffer.h>
#include <storage/cpp/database.h>
#include <storage/cpp/storage_manager.h>
#include <storage/cpp/data_service.h>
#include <storage/cpp/object.h>
#include <storage/cpp/index_entry.h>
#include <storage/cpp/db_meta.h>
#include <storage/cpp/compressor.h>
#include <storage/cpp/decompress_helper.h>
#include <storage/cpp/decompressing_streambuf.h>
#include <common/cpp/write_context_hash.h>
#include <nanobind/operators.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/pair.h>
#include <istream>

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
    FLY_EXPORT_READONLY_PROPERTY("size", &FlyBuffer::size);

FLY_EXPORT_CLASS(Database, "EXStgDatabase")
    FLY_EXPORT_DEF("_write_pickle_bytes", [](Database& db, const CMString& name,
                                              fly_export::bytes data,
                                              const CMString& py_name,
                                              bool backup) -> CMString {
        return db.write_pickle_bytes(name, data.c_str(),
                                       static_cast<int64_t>(data.size()), py_name, backup);
    })
    FLY_EXPORT_DEF("_write_pickle_bytes", [](Database& db, const CMString& name,
                                              fly_export::bytes data,
                                              const CMString& py_name) -> CMString {
        return db.write_pickle_bytes(name, data.c_str(),
                                       static_cast<int64_t>(data.size()), py_name, false);
    })
    FLY_EXPORT_DEF("_read_streaming", [](Database& db, const CMString& name, bool backup) -> fly_export::tuple {
        auto [comp_data, py_name] = db.read_object_compressed(name, backup);
        return fly_export::make_tuple(
            fly_export::bytes(comp_data.data(), comp_data.size()),
            py_name
        );
    })
    FLY_EXPORT_DEF("_read_streaming", [](Database& db, const CMString& name) -> fly_export::tuple {
        auto [comp_data, py_name] = db.read_object_compressed(name, false);
        return fly_export::make_tuple(
            fly_export::bytes(comp_data.data(), comp_data.size()),
            py_name
        );
    })
    FLY_EXPORT_DEF("_decompress_bytes", [](Database&, fly_export::bytes b) -> fly_export::bytes {
        CMString raw(b.c_str(), b.size());
        CMString result = fly::decompress_raw_data(raw);
        return fly_export::bytes(result.data(), result.size());
    })
    FLY_EXPORT_DEF("_compress_pickle_bytes", [](Database& db, fly_export::bytes data,
                                                 const CMString& py_name) -> fly_export::bytes {
        CMString compressed = db.compress_pickle_bytes(data.c_str(),
                                                        static_cast<int64_t>(data.size()),
                                                        py_name);
        return fly_export::bytes(compressed.data(), compressed.size());
    })
    FLY_EXPORT_DEF("write_object_raw", [](Database& db, const CMString& name, const CMString& data, bool backup) -> CMString {
        return db.write_pickle_bytes(name, data.data(), static_cast<int64_t>(data.size()), "bytes", backup);
    })
    FLY_EXPORT_DEF("write_object_raw", [](Database& db, const CMString& name, const CMString& data) -> CMString {
        return db.write_pickle_bytes(name, data.data(), static_cast<int64_t>(data.size()), "bytes", false);
    })
    FLY_EXPORT_DEF("read_object_raw", [](Database& db, const CMString& name, bool backup) -> CMString {
        auto [comp_data, py_name] = db.read_object_compressed(name, backup);
        if (comp_data.empty()) return {};
        return fly::decompress_raw_data(comp_data);
    })
    FLY_EXPORT_DEF("read_object_raw", [](Database& db, const CMString& name) -> CMString {
        auto [comp_data, py_name] = db.read_object_compressed(name, false);
        if (comp_data.empty()) return {};
        return fly::decompress_raw_data(comp_data);
    })
    FLY_EXPORT_METHOD("backup_object", &Database::backup_object)
    FLY_EXPORT_METHOD("freeze", &Database::freeze)
    FLY_EXPORT_METHOD("is_frozen", &Database::is_frozen)
    FLY_EXPORT_METHOD("load_meta", &Database::load_meta)
    FLY_EXPORT_METHOD("get_db_id", &Database::get_db_id)
    FLY_EXPORT_METHOD("set_db_id", &Database::set_db_id)
    FLY_EXPORT_METHOD("get_base_path", &Database::get_base_path)
    FLY_EXPORT_METHOD("get_data_path", &Database::get_data_path)
    FLY_EXPORT_METHOD("get_obj_name", &Database::get_obj_name)
    FLY_EXPORT_METHOD("get_writer_id", &Database::get_writer_id)
    FLY_EXPORT_METHOD("reset", &Database::reset)
    FLY_EXPORT_METHOD("remove_object", &Database::remove_object)
    FLY_EXPORT_DEF("_put_temp", [](Database& db, const CMString& name, fly_export::bytes data) {
        CMString compressed(data.c_str(), data.size());
        db.put_temp(name, compressed);
    })
    FLY_EXPORT_DEF("_get_temp", [](Database& db, const CMString& name) -> fly_export::tuple {
        auto [found, data] = db.get_temp(name);
        if (!found) return fly_export::make_tuple(false, fly_export::bytes());
        return fly_export::make_tuple(true, fly_export::bytes(data.data(), data.size()));
    })
    FLY_EXPORT_DEF("_has_temp", &Database::has_temp)
    FLY_EXPORT_DEF("_remove_temp", &Database::remove_temp)
    FLY_EXPORT_DEF("_mark_temp", [](Database& db, const CMString& name) {
        db.mark_temp(name);
    })
    FLY_EXPORT_DEF("_put_temp_data", [](Database& db, const CMString& name, fly_export::bytes compressed_data) {
        CMString data(compressed_data.c_str(), compressed_data.size());
        db.put_temp_data(name, data);
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
    FLY_EXPORT_METHOD("stop_transfer_server", &fly::DataService::stop_transfer_server)
    FLY_EXPORT_METHOD("has_database", &fly::DataService::has_database);

FLY_EXPORT_FUNCTION("ex_stg_get_data_service", []() { return fly::DataService::instance(); });

FLY_EXPORT_CLASS(StorageManager, "EXStgStorageManager")
    FLY_EXPORT_METHOD("close_all", &StorageManager::close_all)
    FLY_EXPORT_METHOD("reset", &StorageManager::reset)
    FLY_EXPORT_DEF("get_or_create_database", [](StorageManager& sm, const CMString& base_path) -> CMSharedPtr<Database> {
        return sm.get_or_create_database(base_path);
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
    FLY_EXPORT_INIT(CMString, int64_t)
    FLY_EXPORT_READONLY_ATTR("db_id", &DbMeta::db_id_)
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

FLY_EXPORT_FUNCTION("ex_stg_create_database", [](const CMString& base_path, const CMString& data_path, uint64_t writer_id) -> CMSharedPtr<Database> {
    return CMMakeShared<Database>(base_path, data_path, writer_id);
});

FLY_EXPORT_FUNCTION("ex_stg_create_database_with_id", [](const CMString& base_path, const CMString& data_path, uint64_t writer_id, const CMString& db_id) -> CMSharedPtr<Database> {
    return CMMakeShared<Database>(base_path, data_path, writer_id, "", db_id);
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

FLY_EXPORT_FUNCTION("ex_stg_mark_temp_entry", [](const CMString& object_name, fly_export::bytes compressed_data) {
    CMString data(compressed_data.c_str(), compressed_data.size());
    fly::DataService::instance()->mark_temp_entry(object_name, data);
});

FLY_EXPORT_FUNCTION("ex_stg_unmark_temp_entry", [](const CMString& object_name) {
    fly::DataService::instance()->unmark_temp_entry(object_name);
});
}
