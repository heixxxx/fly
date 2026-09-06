#include <storage/cpp/local_index.h>
#include <log/cpp/logger.h>
#include <common/serialization/cpp/serialization_macros.h>
#include <filesystem>

namespace {

struct IndexData {
    CMUnorderedMap<CMString, CMVector<IndexEntry>> entries_;

    FLY_SERIALIZE(entries_)
};

struct AddRecord {
    CMString object_name_;
    CMVector<IndexEntry> entries_;

    FLY_SERIALIZE(object_name_, entries_)
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
    // 标记记录(BEGIN/END/ABORT)携带空 body(body_size=0)，需允许。
    return body_size >= 0;
}

}

LocalIndex::LocalIndex(const CMString& idx_path)
    : idx_path_(idx_path) {}

LocalIndex::~LocalIndex() = default;

void LocalIndex::add_entry(const IndexEntry& entry) {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_[entry.object_name_].push_back(entry);
    pending_adds_[entry.object_name_].push_back(entry);
    pending_removes_.erase(entry.object_name_);
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

std::optional<IndexEntry> LocalIndex::find_entry(const CMString& object_name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entries_.find(object_name);
    if (it == entries_.end() || it->second.empty()) {
        return std::nullopt;
    }
    return it->second.front();
}

std::optional<CMVector<IndexEntry>> LocalIndex::find_all_entries(const CMString& object_name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entries_.find(object_name);
    if (it == entries_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::ofstream& LocalIndex::append_stream() {
    if (idx_append_stream_.is_open()) {
        return idx_append_stream_;
    }
    // 惰性打开（append 模式）。失败由调用方检查 is_open 判定。
    idx_append_stream_.open(idx_path_, std::ios::binary | std::ios::app);
    return idx_append_stream_;
}

void LocalIndex::reset_append_stream() {
    if (idx_append_stream_.is_open()) {
        idx_append_stream_.close();
    }
}

void LocalIndex::append_add(const CMString& object_name, const CMVector<IndexEntry>& entries) {
    AddRecord record;
    record.object_name_ = object_name;
    record.entries_ = entries;

    CMString body;
    FLY_ENCODE(record, body);

    std::ofstream& ofs = append_stream();
    if (!ofs.is_open()) {
        ERR("Failed to open index file: {}", idx_path_); return;
    }

    int64_t body_size = static_cast<int64_t>(body.size());

    write_header(ofs, IdxOpType::ADD, body_size);
    ofs.write(body.data(), body.size());
}

void LocalIndex::append_remove(const CMString& object_name) {
    AddRecord record;
    record.object_name_ = object_name;

    CMString body;
    FLY_ENCODE(record, body);

    std::ofstream& ofs = append_stream();
    if (!ofs.is_open()) {
        ERR("Failed to open index file: {}", idx_path_); return;
    }

    write_header(ofs, IdxOpType::REMOVE, static_cast<int64_t>(body.size()));
    ofs.write(body.data(), body.size());
}

void LocalIndex::append_marker(IdxOpType op) {
    // 标记记录仅 8 字节 header，body 为空。不修改 entries_ —— BEGIN/END/ABORT
    // 的 pending 区语义在 load() 时解释，写入时只落盘标记本身。
    std::ofstream& ofs = append_stream();
    if (!ofs.is_open()) {
        ERR("Failed to open index file: {}", idx_path_); return;
    }
    write_header(ofs, op, 0);
    ofs.flush();
}

void LocalIndex::mark_begin() {
    append_marker(IdxOpType::BEGIN);
}

void LocalIndex::mark_end() {
    append_marker(IdxOpType::END);
}

void LocalIndex::mark_abort() {
    append_marker(IdxOpType::ABORT);
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

    // 复用持久追加流后必须显式 flush —— idx 文件是 WAL，save() 返回即要求
    // 记录已落盘（崩溃后 load() 要能读到已提交的段）。原实现每次 append 用
    // 独立 ofstream，靠析构 flush；现在复用流，save 末尾显式 flush 保同等语义。
    if (idx_append_stream_.is_open()) {
        idx_append_stream_.flush();
    }
}

void LocalIndex::save_legacy() {
    IndexData data;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        data.entries_ = entries_;
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

    // truncate 重写了文件，持久追加流的 fd（若已打开）位置/状态不再可靠，重置。
    reset_append_stream();

    std::lock_guard<std::mutex> lock(mutex_);
    modified_ = false;
}

void LocalIndex::load() {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.clear();
    pending_adds_.clear();
    pending_removes_.clear();
    had_unclosed_segment_ = false;

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

    // 新格式：首字节是任一已知 op（含 BEGIN/END/ABORT 标记）。
    bool is_new_format = (first_byte == static_cast<uint8_t>(IdxOpType::ADD) ||
                          first_byte == static_cast<uint8_t>(IdxOpType::REMOVE) ||
                          first_byte == static_cast<uint8_t>(IdxOpType::BEGIN) ||
                          first_byte == static_cast<uint8_t>(IdxOpType::END) ||
                          first_byte == static_cast<uint8_t>(IdxOpType::ABORT));

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
                entries_ = std::move(data.entries_);
                modified_ = false;
                return;
            }
        }

        entries_.clear();
        modified_ = false;
        return;
    }

    // 新格式 op log：带 pending 区的事务状态机。
    //
    //   显式事务：BEGIN 后的 ADD 进 pending，END 提交，ABORT 丢弃。
    //   隐式事务：段外 ADD（无 BEGIN 包裹）直接进 entries_（master 写入兼容）。
    //   崩溃遗留：EOF 时 pending 非空 → 自然丢弃。
    bool in_segment = false;
    CMUnorderedMap<CMString, CMVector<IndexEntry>> pending;

    while (true) {
        IdxOpType op;
        int64_t body_size = 0;
        if (!read_header(ifs, op, body_size)) break;

        // 截断容错：剩余字节不足一个完整 body，停止解析（崩溃留下的半截记录）。
        if (ifs.tellg() + body_size > file_size) break;

        CMString body;
        if (body_size > 0) {
            body.resize(static_cast<size_t>(body_size));
            ifs.read(body.data(), body_size);
            if (!ifs) break;
        }

        if (op == IdxOpType::ADD) {
            try {
                AddRecord record;
                FLY_DECODE(body, AddRecord, record);
                if (in_segment) {
                    pending[record.object_name_] = std::move(record.entries_);
                } else {
                    // 隐式事务：段外 ADD 直接生效（向后兼容 + master 写入）。
                    entries_[record.object_name_] = std::move(record.entries_);
                }
            } catch (...) {}
        } else if (op == IdxOpType::REMOVE) {
            try {
                AddRecord record;
                FLY_DECODE(body, AddRecord, record);
                entries_.erase(record.object_name_);
                pending.erase(record.object_name_);
            } catch (...) {}
        } else if (op == IdxOpType::BEGIN) {
            in_segment = true;
            pending.clear();
        } else if (op == IdxOpType::END) {
            // 提交 pending 进 entries_。
            for (auto& [k, v] : pending) {
                entries_[k] = std::move(v);
            }
            pending.clear();
            in_segment = false;
        } else if (op == IdxOpType::ABORT) {
            // 丢弃 pending（整段撤销）。
            pending.clear();
            in_segment = false;
        }
    }

    // 诊断：EOF 时仍处于段内且 pending 非空 → 崩溃遗留的未闭合段。
    had_unclosed_segment_ = in_segment && !pending.empty();
    // pending 中未提交的记录在此自然丢弃，不入 entries_。

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

        // compact 只保留已提交的 entries_，产物是无标记的干净 idx（段外 ADD 形式）。
        for (auto& [name, entries] : entries_) {
            AddRecord record;
            record.object_name_ = name;
            record.entries_ = entries;

            CMString body;
            FLY_ENCODE(record, body);

            int64_t body_size = static_cast<int64_t>(body.size());

            write_header(ofs, IdxOpType::ADD, body_size);
            ofs.write(body.data(), body.size());
        }
    }

    std::filesystem::rename(tmp_path, idx_path_);

    // rename 后旧 fd 指向被 unlink 的旧 inode，必须重置追加流。
    reset_append_stream();
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

bool LocalIndex::had_unclosed_segment() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return had_unclosed_segment_;
}
