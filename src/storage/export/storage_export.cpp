#include <export/cpp/export_macros.h>
#include <serialization/cpp/serialization_macros.h>
#include <storage/cpp/database.h>
#include <storage/cpp/storage_manager.h>
#include <storage/cpp/object.h>
#include <storage/cpp/index_entry.h>
#include <storage/cpp/db_meta.h>
#include <storage/cpp/compressor.h>
#include <nanobind/operators.h>

FLY_EXPORT_MODULE_BEGIN(_fly_storage)

fly_export::enum_<CompressionType>(m, "CompressionType")
    .value("NONE", CompressionType::NONE)
    .value("LZ4", CompressionType::LZ4)
    .value("ZLIB", CompressionType::ZLIB)
    .value("ZSTD", CompressionType::ZSTD);

FLY_EXPORT_FUNCTION_WITH_NAME(m, "compression_type_from_name", [](const CMString& name) -> CompressionType {
    return CompressorFactory::type_from_name(name);
});

FLY_EXPORT_FUNCTION_WITH_NAME(m, "compression_name_from_type", [](CompressionType type) -> CMString {
    return CompressorFactory::name_from_type(type);
});

fly_export::class_<Database>(m, "Database")
    .def("write_object", [](Database& db, const CMString& name, fly_export::object obj) -> CMString {
        auto state = obj.attr("__getstate__")();
        auto bytes = fly_export::cast<fly_export::bytes>(state);
        CMString data(bytes.c_str(), bytes.size());
        auto type_obj = obj.type();
        CMString py_name = fly_export::cast<CMString>(type_obj.attr("__name__"));
        return db.write_object_typed(name, data, py_name);
    })
    .def("read_object", [](Database& db, const CMString& name) -> fly_export::object {
        ReadResult result = db.read_object_typed(name);
        if (!result.py_name.empty()) {
            auto sys = fly_export::module_::import_("sys");
            auto modules = sys.attr("modules");
            fly_export::object cls = fly_export::none();
            if (fly_export::hasattr(modules, "__contains__") &&
                modules.attr("__contains__")("_fly_storage")) {
                auto storage_mod = modules["_fly_storage"];
                if (fly_export::hasattr(storage_mod, result.py_name.c_str())) {
                    cls = storage_mod.attr(result.py_name.c_str());
                }
            }
            if (cls.is_none()) {
                auto items = modules.attr("items")();
                for (auto item : items) {
                    auto pair = fly_export::cast<fly_export::tuple>(item);
                    auto mod = pair[1];
                    if (fly_export::hasattr(mod, result.py_name.c_str())) {
                        cls = mod.attr(result.py_name.c_str());
                        break;
                    }
                }
            }
            if (!cls.is_none()) {
                fly_export::object obj = cls.attr("__new__")(cls);
                fly_export::bytes state_bytes(
                    reinterpret_cast<const char*>(result.data_buffer.data()),
                    result.data_buffer.size());
                obj.attr("__setstate__")(state_bytes);
                return obj;
            }
        }
        return fly_export::bytes(
            reinterpret_cast<const char*>(result.data_buffer.data()),
            result.data_buffer.size());
    })
    .def("write_object_raw", [](Database& db, const CMString& name, const CMString& data) -> CMString {
        return db.write_object(name, data);
    })
    .def("read_object_raw", [](Database& db, const CMString& name) -> CMString {
        return db.read_object(name);
    })
    .def("freeze", &Database::freeze)
    .def("is_frozen", &Database::is_frozen)
    .def("load_meta", &Database::load_meta)
    .def("get_db_id", &Database::get_db_id)
    .def("get_base_path", &Database::get_base_path)
    .def("get_data_path", &Database::get_data_path)
    .def("reset", &Database::reset);

FLY_EXPORT_CLASS_NO_INIT(m, StorageManager,
    FLY_EXPORT_METHOD(close_all, &StorageManager::close_all)
    FLY_EXPORT_METHOD(reset, &StorageManager::reset)
    .def("get_or_create_database", [](StorageManager& sm, const CMString& base_path) -> std::shared_ptr<Database> {
        return sm.get_or_create_database(base_path);
    })
);

fly_export::class_<IndexEntry>(m, "IndexEntry")
    .def(fly_export::init<>())
    .def_ro("object_name", &IndexEntry::object_name)
    .def_ro("file_name", &IndexEntry::file_name)
    .def_ro("offset", &IndexEntry::offset)
    .def_ro("size", &IndexEntry::size)
    .def_ro("is_large", &IndexEntry::is_large)
    .def_ro("block_count", &IndexEntry::block_count)
    .def_ro("compression_type", &IndexEntry::compression_type)
    .def("__getstate__", [](const IndexEntry& obj) -> fly_export::bytes {
        std::string serialized;
        FLY_ENCODE(obj, serialized);
        return fly_export::bytes(serialized.data(), serialized.size());
    })
    .def("__setstate__", [](IndexEntry& obj, fly_export::bytes b) {
        std::string data(b.c_str(), b.size());
        FLY_DECODE(data, IndexEntry, obj);
    });

fly_export::class_<DbMeta>(m, "DbMeta")
    .def(fly_export::init<>())
    .def_ro("db_id", &DbMeta::db_id)
    .def_ro("base_path", &DbMeta::base_path)
    .def_ro("created_at", &DbMeta::created_at)
    .def_ro("frozen_at", &DbMeta::frozen_at)
    .def("__getstate__", [](const DbMeta& obj) -> fly_export::bytes {
        std::string serialized;
        FLY_ENCODE(obj, serialized);
        return fly_export::bytes(serialized.data(), serialized.size());
    })
    .def("__setstate__", [](DbMeta& obj, fly_export::bytes b) {
        std::string data(b.c_str(), b.size());
        FLY_DECODE(data, DbMeta, obj);
    });

fly_export::class_<WorkerInfo>(m, "WorkerInfo")
    .def(fly_export::init<>())
    .def_ro("worker_id", &WorkerInfo::worker_id)
    .def_ro("host", &WorkerInfo::host)
    .def_ro("role", &WorkerInfo::role)
    .def_ro("data_path", &WorkerInfo::data_path)
    .def_ro("idx_file", &WorkerInfo::idx_file)
    .def_ro("idx_entry_count", &WorkerInfo::idx_entry_count)
    .def_ro("launch_command", &WorkerInfo::launch_command)
    .def("__getstate__", [](const WorkerInfo& obj) -> fly_export::bytes {
        std::string serialized;
        FLY_ENCODE(obj, serialized);
        return fly_export::bytes(serialized.data(), serialized.size());
    })
    .def("__setstate__", [](WorkerInfo& obj, fly_export::bytes b) {
        std::string data(b.c_str(), b.size());
        FLY_DECODE(data, WorkerInfo, obj);
    });

FLY_EXPORT_FUNCTION_REF_WITH_NAME(m, "get_storage_manager", []() -> StorageManager& { return StorageManager::instance(); });

FLY_EXPORT_FUNCTION_WITH_NAME(m, "create_database", [](const CMString& base_path, const CMString& data_path) -> std::shared_ptr<Database> {
    return std::make_shared<Database>(base_path, data_path);
});

FLY_EXPORT_MODULE_END()