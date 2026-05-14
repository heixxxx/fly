#pragma once

#include <storage/cpp/index_entry.h>
#include <common/cpp/common_types.h>
#include <cstdint>
#include <fstream>

class LocalIndex {
public:
    explicit LocalIndex(const CMString& idx_path);
    ~LocalIndex();

    LocalIndex(const LocalIndex&) = delete;
    LocalIndex& operator=(const LocalIndex&) = delete;

    void add_entry(const IndexEntry& entry);
    bool remove_entry(const CMString& object_name);
    IndexEntry* find_entry(const CMString& object_name);
    CMVector<IndexEntry>* find_all_entries(const CMString& object_name);

    void save();
    void load();

    int64_t entry_count() const;
    CMVector<IndexEntry> get_all_entries() const;

private:
    CMString idx_path_;
    CMUnorderedMap<CMString, CMVector<IndexEntry>> entries_;
    bool modified_ = false;
};