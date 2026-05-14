#pragma once

#include <storage/cpp/database.h>
#include <storage/cpp/data_writer.h>
#include <common/cpp/common_types.h>
#include <unordered_map>
#include <memory>

class StorageManager {
public:
    static StorageManager& instance();

    StorageManager(const StorageManager&) = delete;
    StorageManager& operator=(const StorageManager&) = delete;

    std::shared_ptr<Database> get_or_create_database(
        const CMString& base_path,
        const CMString& data_path = ""
    );

    std::shared_ptr<DataWriter> get_writer(uint64_t worker_id);

    void close_all();
    void reset();

private:
    StorageManager() = default;

    CMMap<CMString, std::shared_ptr<Database>> databases_;
    CMMap<uint64_t, std::shared_ptr<DataWriter>> writers_;
};