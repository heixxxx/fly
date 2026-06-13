#pragma once

#include <common/cpp/common_types.h>
#include <common/cpp/concurrent_map.h>
#include <cstdint>
#include <atomic>

namespace fly {

class TempStore {
public:
    explicit TempStore(int64_t max_bytes = 0);
    ~TempStore();

    TempStore(const TempStore&) = delete;
    TempStore& operator=(const TempStore&) = delete;

    void put(const CMString& object_name, const CMString& compressed_data);
    std::pair<bool, CMString> get(const CMString& object_name);
    bool has(const CMString& object_name);
    void remove(const CMString& object_name);
    void cleanup_all();

    int64_t mem_bytes() const;
    int64_t max_bytes() const;

private:
    struct MemEntry {
        CMString compressed_data_;
        int64_t size_ = 0;
    };

    int64_t max_bytes_;
    std::atomic<int64_t> mem_bytes_{0};
    ConcurrentUnorderedMap<CMString, MemEntry> mem_;
    ConcurrentUnorderedMap<CMString, CMString> disk_files_;
    CMString tmp_dir_;
};

}  // namespace fly
