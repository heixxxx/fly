#include <storage/cpp/storage_manager.h>
#include <storage/cpp/data_service.h>
#include <filesystem>

namespace fs = std::filesystem;

StorageManager& StorageManager::instance() {
    static StorageManager manager;
    return manager;
}

CMSharedPtr<Database> StorageManager::get_or_create_database(
    const CMString& base_path,
    const CMString& data_path) {

    return databases_.get_or_insert(base_path, [&]() {
        fs::create_directories(base_path);
        if (!data_path.empty()) {
            fs::create_directories(data_path);
        }
        return CMMakeShared<Database>(base_path, data_path);
    });
}

CMSharedPtr<DataWriter> StorageManager::get_writer(uint64_t worker_id) {
    return writers_.get_or_insert(worker_id, [&]() {
        return CMMakeShared<DataWriter>(
            "/tmp/fly_worker_" + std::to_string(worker_id),
            "",
            "",
            1048576
        );
    });
}

void StorageManager::close_all() {
    databases_.iterate([](const CMString& path, CMSharedPtr<Database>& db) {
        if (!db->is_frozen()) {
            db->freeze();
        }
    });
    writers_.iterate([](const uint64_t& id, CMSharedPtr<DataWriter>& writer) {
        writer->close();
    });
    databases_.clear();
    writers_.clear();
}

void StorageManager::reset() {
    databases_.iterate([](const CMString& path, const CMSharedPtr<Database>& db) {
        fly::DataService::instance().unregister_database(db->get_db_id());
    });
    databases_.clear();
    writers_.clear();
}