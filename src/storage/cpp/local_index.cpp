#include <storage/cpp/local_index.h>
#include <log/cpp/logger.h>
#include <serialization/cpp/serialization_macros.h>
#include <filesystem>

namespace {

struct IndexData {
    CMUnorderedMap<CMString, CMVector<IndexEntry>> entries;

    FLY_SERIALIZE(entries)
};

struct AddRecord {
    CMString object_name;
    CMVector<IndexEntry> entries;

    FLY_SERIALIZE(object_name, entries)
};

void write_header(std::ofstream& ofs, IdxOpType op, int64_t body_size) {
    uint8_t buf[8];
    buf[0] = static_cast<uint8_t>(op);
    auto sz = static_cast<uint64_t>(body_size);
    for (int i = 0; i < 7; i++) {
        buf[1 + i] = static_cast<uint8_t>((sz >> (i * 8)) & 0xFF);
    }
    ofs.write(reinterpret_cast<const char*>(buf), 8);
}

bool read_header(std::ifstream& ifs, IdxOpType& op, int64_t& body_size) {
    uint8_t buf[8];
    ifs.read(reinterpret_cast<char*>(buf), 8);
    if (!ifs) return false;
    op = static_cast<IdxOpType>(buf[0]);
    uint64_t sz = 0;
    for (int i = 0; i < 7; i++) {
        sz |= static_cast<uint64_t>(buf[1 + i]) << (i * 8);
    }
    body_size = static_cast<int64_t>(sz);
    return body_size > 0;
}

}

LocalIndex::LocalIndex(const CMString& idx_path)
    : idx_path_(idx_path) {}

LocalIndex::~LocalIndex() = default;

void LocalIndex::add_entry(const IndexEntry& entry) {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_[entry.object_name].push_back(entry);
    pending_adds_[entry.object_name].push_back(entry);
    pending_removes_.erase(entry.object_name);
    modified_ = true;
}

bool LocalIndex::remove_entry(const CMString& object_name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entries_.find(object_name);
    if (it == entries_.end()) {
        return false;
    }
    entries_.erase(it);
    pending_removes_.insert(object_name);
    pending_adds_.erase(object_name);
    modified_ = true;
    return true;
}

IndexEntry* LocalIndex::find_entry(const CMString& object_name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entries_.find(object_name);
    if (it == entries_.end() || it->second.empty()) {
        return nullptr;
    }
    return &(it->second.front());
}

CMVector<IndexEntry>* LocalIndex::find_all_entries(const CMString& object_name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entries_.find(object_name);
    if (it == entries_.end()) {
        return nullptr;
    }
    return &(it->second);
}

void LocalIndex::append_add(const CMString& object_name, const CMVector<IndexEntry>& entries) {
    AddRecord record;
    record.object_name = object_name;
    record.entries = entries;

    CMString body;
    FLY_ENCODE(record, body);

    std::ofstream ofs(idx_path_, std::ios::binary | std::ios::app);
    if (!ofs.is_open()) {
        ERR("Failed to open index file: {}", idx_path_); return;
    }

    int64_t body_size = static_cast<int64_t>(body.size());

    write_header(ofs, IdxOpType::ADD, body_size);
    ofs.write(body.data(), body.size());
}

void LocalIndex::append_remove(const CMString& object_name) {
    AddRecord record;
    record.object_name = object_name;

    CMString body;
    FLY_ENCODE(record, body);

    std::ofstream ofs(idx_path_, std::ios::binary | std::ios::app);
    if (!ofs.is_open()) {
        ERR("Failed to open index file: {}", idx_path_); return;
    }

    write_header(ofs, IdxOpType::REMOVE, static_cast<int64_t>(body.size()));
    ofs.write(body.data(), body.size());
}

void LocalIndex::save() {
    CMUnorderedMap<CMString, CMVector<IndexEntry>> adds_snapshot;
    CMUnorderedSet<CMString> removes_snapshot;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!modified_) return;
        adds_snapshot = std::move(pending_adds_);
        removes_snapshot = std::move(pending_removes_);
        pending_adds_.clear();
        pending_removes_.clear();
        modified_ = false;
    }

    for (auto& [name, entries] : adds_snapshot) {
        append_add(name, entries);
    }
    for (auto& name : removes_snapshot) {
        append_remove(name);
    }
}

void LocalIndex::save_legacy() {
    IndexData data;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        data.entries = entries_;
    }

    CMString bytes;
    FLY_ENCODE(data, bytes);

    std::ofstream ofs(idx_path_, std::ios::binary);
    if (!ofs.is_open()) {
        ERR("Failed to open index file for writing: {}", idx_path_); return;
    }

    int64_t size = static_cast<int64_t>(bytes.size());
    ofs.write(reinterpret_cast<const char*>(&size), sizeof(size));
    ofs.write(bytes.data(), bytes.size());
    ofs.close();

    std::lock_guard<std::mutex> lock(mutex_);
    modified_ = false;
}

void LocalIndex::load() {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.clear();
    pending_adds_.clear();
    pending_removes_.clear();

    if (!std::filesystem::exists(idx_path_)) {
        modified_ = false;
        return;
    }

    std::ifstream ifs(idx_path_, std::ios::binary);
    if (!ifs.is_open()) {
        modified_ = false;
        return;
    }

    ifs.seekg(0, std::ios::end);
    int64_t file_size = ifs.tellg();
    ifs.seekg(0, std::ios::beg);

    if (file_size == 0) {
        modified_ = false;
        return;
    }

    uint8_t first_byte = 0;
    ifs.read(reinterpret_cast<char*>(&first_byte), sizeof(first_byte));
    ifs.seekg(0, std::ios::beg);

    bool is_new_format = (first_byte == static_cast<uint8_t>(IdxOpType::ADD) ||
                          first_byte == static_cast<uint8_t>(IdxOpType::REMOVE));

    if (!is_new_format) {
        int64_t legacy_size = 0;
        ifs.read(reinterpret_cast<char*>(&legacy_size), sizeof(legacy_size));

        if (legacy_size > 0 &&
            static_cast<int64_t>(sizeof(int64_t)) + legacy_size <= file_size) {
            CMString bytes(legacy_size, '\0');
            ifs.read(bytes.data(), legacy_size);
            ifs.close();

            IndexData data;
            bool decoded = false;
            try {
                FLY_DECODE(bytes, IndexData, data);
                decoded = true;
            } catch (...) {}

            if (decoded) {
                entries_ = std::move(data.entries);
                modified_ = false;
                return;
            }
        }

        entries_.clear();
        modified_ = false;
        return;
    }

    while (true) {
        IdxOpType op;
        int64_t body_size = 0;
        if (!read_header(ifs, op, body_size)) break;

        if (ifs.tellg() + body_size > file_size) break;

        CMString body(body_size, '\0');
        ifs.read(body.data(), body_size);
        if (!ifs) break;

        if (op == IdxOpType::ADD) {
            AddRecord record;
            try {
                FLY_DECODE(body, AddRecord, record);
                entries_[record.object_name] = std::move(record.entries);
            } catch (...) {}
        } else if (op == IdxOpType::REMOVE) {
            AddRecord record;
            try {
                FLY_DECODE(body, AddRecord, record);
                entries_.erase(record.object_name);
            } catch (...) {}
        }
    }

    ifs.close();
    modified_ = false;
}

void LocalIndex::compact() {
    CMString tmp_path = idx_path_ + ".compact";
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ofstream ofs(tmp_path, std::ios::binary);
        if (!ofs.is_open()) {
            ERR("Failed to create compact file: {}", tmp_path); return;
        }

        for (auto& [name, entries] : entries_) {
            AddRecord record;
            record.object_name = name;
            record.entries = entries;

            CMString body;
            FLY_ENCODE(record, body);

            int64_t body_size = static_cast<int64_t>(body.size());

            write_header(ofs, IdxOpType::ADD, body_size);
            ofs.write(body.data(), body.size());
        }
    }

    std::filesystem::rename(tmp_path, idx_path_);
}

int64_t LocalIndex::entry_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    int64_t count = 0;
    for (const auto& [key, vec] : entries_) {
        count += static_cast<int64_t>(vec.size());
    }
    return count;
}

CMVector<IndexEntry> LocalIndex::get_all_entries() const {
    std::lock_guard<std::mutex> lock(mutex_);
    CMVector<IndexEntry> result;
    for (const auto& [key, vec] : entries_) {
        for (const auto& entry : vec) {
            result.push_back(entry);
        }
    }
    return result;
}
