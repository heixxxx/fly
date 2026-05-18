#include <storage/cpp/storage_manager.h>
#include <filesystem>

namespace fs = std::filesystem;

StorageManager& StorageManager::instance() {
    static StorageManager manager;
    return manager;
}

CMSharedPtr<Database> StorageManager::get_or_create_database(
    const CMString& base_path,
    const CMString& data_path) {

    auto it = databases_.find(base_path);
    if (it != databases_.end()) {
        return it->second;
    }

    fs::create_directories(base_path);
    if (!data_path.empty()) {
        fs::create_directories(data_path);
    }

    auto db = CMMakeShared<Database>(base_path, data_path);
    databases_[base_path] = db;
    return db;
}

CMSharedPtr<DataWriter> StorageManager::get_writer(uint64_t worker_id) {
    auto it = writers_.find(worker_id);
    if (it != writers_.end()) {
        return it->second;
    }

    auto writer = CMMakeShared<DataWriter>(
        "/tmp/fly_worker_" + std::to_string(worker_id),
        "",
        worker_id,
        4096, 1048576, 65536
    );
    writers_[worker_id] = writer;
    return writer;
}

void StorageManager::close_all() {
    for (auto& [path, db] : databases_) {
        if (!db->is_frozen()) {
            db->freeze();
        }
    }
    for (auto& [id, writer] : writers_) {
        writer->close();
    }
    databases_.clear();
    writers_.clear();
}

void StorageManager::reset() {
    databases_.clear();
    writers_.clear();
}