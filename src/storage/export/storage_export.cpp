#include <export/cpp/export_macros.h>
#include <serialization/cpp/serialization_macros.h>
#include <storage/cpp/database.h>
#include <storage/cpp/storage_manager.h>
#include <storage/cpp/object.h>
#include <storage/cpp/index_entry.h>
#include <storage/cpp/db_meta.h>
#include <storage/cpp/compressor.h>
#include <nanobind/operators.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/pair.h>

FLY_EXPORT_MODULE(_fly_storage) {

FLY_EXPORT_ENUM(CompressionType, "EXStgCompressionType")
    FLY_EXPORT_ENUM_VALUE("NONE", CompressionType::NONE)
    FLY_EXPORT_ENUM_VALUE("LZ4", CompressionType::LZ4)
    FLY_EXPORT_ENUM_VALUE("ZLIB", CompressionType::ZLIB)
    FLY_EXPORT_ENUM_VALUE("ZSTD", CompressionType::ZSTD);

FLY_EXPORT_FUNCTION("compression_type_from_name", [](const CMString& name) -> CompressionType {
    return CompressorFactory::type_from_name(name);
});

FLY_EXPORT_FUNCTION("compression_name_from_type", [](CompressionType type) -> CMString {
    return CompressorFactory::name_from_type(type);
});

FLY_EXPORT_CLASS(Database, "EXStgDatabase")
    FLY_EXPORT_DEF("_write_typed", [](Database& db, const CMString& name,
                                       fly_export::bytes data, const CMString& py_name) -> CMString {
        CMString str_data(data.c_str(), data.size());
        return db.write_object_typed(name, str_data, py_name);
    })
    FLY_EXPORT_DEF("_read_typed", [](Database& db, const CMString& name) -> fly_export::tuple {
        ReadResult result = db.read_object_typed(name);
        return fly_export::make_tuple(
            fly_export::bytes(
                reinterpret_cast<const char*>(result.data_buffer.data()),
                result.data_buffer.size()),
            result.py_name
        );
    })
    FLY_EXPORT_DEF("write_object_raw", [](Database& db, const CMString& name, const CMString& data) -> CMString {
        return db.write_object(name, data);
    })
    FLY_EXPORT_DEF("read_object_raw", [](Database& db, const CMString& name) -> CMString {
        return db.read_object(name);
    })
    FLY_EXPORT_METHOD("freeze", &Database::freeze)
    FLY_EXPORT_METHOD("is_frozen", &Database::is_frozen)
    FLY_EXPORT_METHOD("load_meta", &Database::load_meta)
    FLY_EXPORT_METHOD("get_db_id", &Database::get_db_id)
    FLY_EXPORT_METHOD("get_base_path", &Database::get_base_path)
    FLY_EXPORT_METHOD("get_data_path", &Database::get_data_path)
    FLY_EXPORT_METHOD("reset", &Database::reset);

FLY_EXPORT_CLASS(StorageManager, "EXStgStorageManager")
    FLY_EXPORT_METHOD("close_all", &StorageManager::close_all)
    FLY_EXPORT_METHOD("reset", &StorageManager::reset)
    FLY_EXPORT_DEF("get_or_create_database", [](StorageManager& sm, const CMString& base_path) -> std::shared_ptr<Database> {
        return sm.get_or_create_database(base_path);
    });

FLY_EXPORT_CLASS(IndexEntry, "EXStgIndexEntry")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_ATTR("object_name", &IndexEntry::object_name)
    FLY_EXPORT_READONLY_ATTR("file_name", &IndexEntry::file_name)
    FLY_EXPORT_READONLY_ATTR("offset", &IndexEntry::offset)
    FLY_EXPORT_READONLY_ATTR("size", &IndexEntry::size)
    FLY_EXPORT_READONLY_ATTR("is_large", &IndexEntry::is_large)
    FLY_EXPORT_READONLY_ATTR("block_count", &IndexEntry::block_count)
    FLY_EXPORT_READONLY_ATTR("compression_type", &IndexEntry::compression_type)
    FLY_EXPORT_SERIALIZE(IndexEntry);

FLY_EXPORT_CLASS(DbMeta, "EXStgDbMeta")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_ATTR("db_id", &DbMeta::db_id)
    FLY_EXPORT_READONLY_ATTR("base_path", &DbMeta::base_path)
    FLY_EXPORT_READONLY_ATTR("created_at", &DbMeta::created_at)
    FLY_EXPORT_READONLY_ATTR("frozen_at", &DbMeta::frozen_at)
    FLY_EXPORT_SERIALIZE(DbMeta);

FLY_EXPORT_CLASS(WorkerInfo, "EXStgWorkerInfo")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_ATTR("worker_id", &WorkerInfo::worker_id)
    FLY_EXPORT_READONLY_ATTR("host", &WorkerInfo::host)
    FLY_EXPORT_READONLY_ATTR("role", &WorkerInfo::role)
    FLY_EXPORT_READONLY_ATTR("data_path", &WorkerInfo::data_path)
    FLY_EXPORT_READONLY_ATTR("idx_file", &WorkerInfo::idx_file)
    FLY_EXPORT_READONLY_ATTR("idx_entry_count", &WorkerInfo::idx_entry_count)
    FLY_EXPORT_READONLY_ATTR("launch_command", &WorkerInfo::launch_command)
    FLY_EXPORT_SERIALIZE(WorkerInfo);

FLY_EXPORT_FUNCTION_REF("get_storage_manager", []() -> StorageManager& { return StorageManager::instance(); });

FLY_EXPORT_FUNCTION("create_database", [](const CMString& base_path, const CMString& data_path) -> std::shared_ptr<Database> {
    return std::make_shared<Database>(base_path, data_path);
});

// Cross-language test helper: C++ writes a typed object into a Database created by Python
FLY_EXPORT_FUNCTION("cpp_write_index_entry", [](Database& db, const CMString& key) -> CMString {
    IndexEntry entry;
    entry.object_name = key;
    entry.file_name = "cpp_generated.dat";
    entry.offset = 12345;
    entry.size = 67890;
    entry.is_large = false;
    entry.block_count = 0;
    CMString serialized;
    FLY_ENCODE(entry, serialized);
    return db.write_object_typed(key, serialized, "EXStgIndexEntry");
});

}
