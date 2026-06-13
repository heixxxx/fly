#pragma once

#include <storage/cpp/database.h>
#include <storage/cpp/data_writer.h>
#include <common/cpp/common_types.h>
#include <common/cpp/concurrent_map.h>
#include <memory>

class StorageManager {
public:
    StorageManager();

    static CMSharedPtr<StorageManager> instance();

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
    ConcurrentUnorderedMap<CMString, CMSharedPtr<Database>> databases_;
    ConcurrentUnorderedMap<uint64_t, CMSharedPtr<DataWriter>> writers_;
};