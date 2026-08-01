#include <storage/cpp/storage_manager.h>
#include <storage/cpp/data_service.h>
#include <filesystem>

namespace fs = std::filesystem;

CMSharedPtr<StorageManager> StorageManager::instance() {
    static CMSharedPtr<StorageManager> inst = CMMakeShared<StorageManager>();
    return inst;
}

StorageManager::StorageManager() = default;

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

void StorageManager::close_all() {
    databases_.iterate([](const CMString& path, CMSharedPtr<Database>& db) {
        if (!db->is_frozen()) {
            db->freeze();
        }
    });
    databases_.clear();
}

void StorageManager::reset() {
    databases_.iterate([](const CMString& path, const CMSharedPtr<Database>& db) {
        fly::DataService::instance()->unregister_database(db->get_db_path());
    });
    databases_.clear();
}