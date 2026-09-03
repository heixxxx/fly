#pragma once

#include <storage/cpp/database.h>
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
        const CMString& db_path,
        const CMString& data_path = ""
    );

    void close_all();
    void reset();

private:
    using DBMap = CMUnorderedMap<CMString, CMSharedPtr<Database>>;
    ConcurrentUnorderedMap<CMString, CMSharedPtr<Database>> databases_;
};