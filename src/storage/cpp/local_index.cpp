#include <storage/cpp/local_index.h>
#include <serialization/cpp/serialization_macros.h>

namespace {

struct IndexData {
    CMUnorderedMap<CMString, CMVector<IndexEntry>> entries;

    FLY_SERIALIZE(entries)
};

}

LocalIndex::LocalIndex(const CMString& idx_path)
    : idx_path_(idx_path) {}

LocalIndex::~LocalIndex() = default;

void LocalIndex::add_entry(const IndexEntry& entry) {
    entries_[entry.object_name].push_back(entry);
    modified_ = true;
}

bool LocalIndex::remove_entry(const CMString& object_name) {
    auto it = entries_.find(object_name);
    if (it == entries_.end()) {
        return false;
    }
    entries_.erase(it);
    modified_ = true;
    return true;
}

IndexEntry* LocalIndex::find_entry(const CMString& object_name) {
    auto it = entries_.find(object_name);
    if (it == entries_.end() || it->second.empty()) {
        return nullptr;
    }
    return &(it->second.front());
}

CMVector<IndexEntry>* LocalIndex::find_all_entries(const CMString& object_name) {
    auto it = entries_.find(object_name);
    if (it == entries_.end()) {
        return nullptr;
    }
    return &(it->second);
}

void LocalIndex::save() {
    IndexData data;
    data.entries = entries_;

    CMString bytes;
    FLY_ENCODE(data, bytes);

    std::ofstream ofs(idx_path_, std::ios::binary);
    if (!ofs.is_open()) {
        throw std::runtime_error("Failed to open index file for writing: " + idx_path_);
    }

    int64_t size = static_cast<int64_t>(bytes.size());
    ofs.write(reinterpret_cast<const char*>(&size), sizeof(size));
    ofs.write(bytes.data(), bytes.size());
    ofs.close();

    modified_ = false;
}

void LocalIndex::load() {
    std::ifstream ifs(idx_path_, std::ios::binary);
    if (!ifs.is_open()) {
        entries_.clear();
        modified_ = false;
        return;
    }

    int64_t size = 0;
    ifs.read(reinterpret_cast<char*>(&size), sizeof(size));
    if (size <= 0) {
        entries_.clear();
        modified_ = false;
        return;
    }

    CMString bytes(size, '\0');
    ifs.read(bytes.data(), size);
    ifs.close();

    IndexData data;
    FLY_DECODE(bytes, IndexData, data);

    entries_ = std::move(data.entries);
    modified_ = false;
}

int64_t LocalIndex::entry_count() const {
    int64_t count = 0;
    for (const auto& [key, vec] : entries_) {
        count += static_cast<int64_t>(vec.size());
    }
    return count;
}

CMVector<IndexEntry> LocalIndex::get_all_entries() const {
    CMVector<IndexEntry> result;
    for (const auto& [key, vec] : entries_) {
        for (const auto& entry : vec) {
            result.push_back(entry);
        }
    }
    return result;
}