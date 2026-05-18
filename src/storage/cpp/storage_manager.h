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

    CMSharedPtr<Database> get_or_create_database(
        const CMString& base_path,
        const CMString& data_path = ""
    );

    CMSharedPtr<DataWriter> get_writer(uint64_t worker_id);

    void close_all();
    void reset();

private:
    StorageManager() = default;

    CMMap<CMString, CMSharedPtr<Database>> databases_;
    CMMap<uint64_t, CMSharedPtr<DataWriter>> writers_;
};