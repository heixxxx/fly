#include <storage/cpp/data_reader.h>
#include <log/cpp/logger.h>
#include <filesystem>
#include <stdexcept>

namespace fs = std::filesystem;

DataReader::DataReader(
    const CMString& db_path,
    const CMString& data_path,
    const CMString& writer_id
)
    : db_path_(db_path)
    , data_path_(data_path)
    , writer_id_(writer_id) {

    CMString read_dir = data_path_.empty() ? db_path_ : data_path_;
    CMString idx_path = db_path_ + "/" + writer_id_ + ".idx";

    index_ = CMMakeUnique<LocalIndex>(idx_path);
    if (fs::exists(idx_path)) {
        index_->load();
    }
}

DataReader::~DataReader() = default;

bool DataReader::exists(const CMString& object_name) {
    return index_->find_entry(object_name).has_value();
}

CMString DataReader::find_file_path(const CMString& file_name) {
    return find_file_path(file_name, db_path_, data_path_);
}

CMString DataReader::find_file_path(const CMString& file_name,
                                    const CMString& db_path,
                                    const CMString& data_path) {
    if (!data_path.empty()) {
        CMString local_path = data_path + "/" + file_name;
        if (fs::exists(local_path)) {
            return local_path;
        }
    }

    CMString db_path_file = db_path + "/" + file_name;
    if (fs::exists(db_path_file)) {
        return db_path_file;
    }

    // merge 产物目录（merge_db 默认 data_path = "{db_path}.merged_data"）。
    // 跨进程 load_db 的 worker 以 data_path="" 注册（IdxLoad 命令无 data_path
    // 字段），merged db 的 .dat 只能在此命中——前两候选 miss 后的最后探测。
    CMString merged_file = db_path + ".merged_data/" + file_name;
    if (fs::exists(merged_file)) {
        return merged_file;
    }

    ERR("Data file not found: {}", file_name);
    return {};
}

std::optional<IndexEntry> DataReader::find_entry(const CMString& object_name) {
    return index_->find_entry(object_name);
}

std::optional<CMVector<IndexEntry>> DataReader::find_all_entries(const CMString& object_name) {
    return index_->find_all_entries(object_name);
}

FlyBufferPtr DataReader::read_raw_bytes(const CMString& object_name) {
    auto entry = index_->find_entry(object_name);
    if (!entry.has_value()) {
        ERR("read_raw_bytes: object not found: {}", object_name);
        return nullptr;
    }
    return read_raw_bytes(entry.value());
}

FlyBufferPtr DataReader::read_raw_bytes(const IndexEntry& entry) {
    CMString file_path = find_file_path(entry.file_name_);
    return read_from_file(file_path, entry.offset_, entry.size_);
}

FlyBufferPtr DataReader::read_raw_from_entry(const IndexEntry& entry,
                                            const CMString& db_path,
                                            const CMString& data_path) {
    // 与 read_raw_bytes(IndexEntry&) 等价，但不构造完整 DataReader、不 load idx。
    // 调用方（DataService::do_read_raw_entries）已从 local_idx_ 内存索引取得 entry，
    // 此处只需用 db_path/data_path 定位文件 + 区间读取。
    CMString file_path = find_file_path(entry.file_name_, db_path, data_path);
    return read_from_file(file_path, entry.offset_, entry.size_);
}

FlyBufferPtr DataReader::read_from_file(const CMString& file_path, int64_t offset, int64_t size) {
    std::ifstream ifs(file_path, std::ios::binary);
    if (!ifs.is_open()) {
        ERR("Failed to open data file: {}", file_path);
        return nullptr;
    }

    ifs.seekg(offset);
    CMString buffer(static_cast<size_t>(size), '\0');
    ifs.read(buffer.data(), static_cast<std::streamsize>(size));

    if (!ifs) {
        ERR("Failed to read data from file: {}", file_path);
        return nullptr;
    }

    // Zero-copy: move the read buffer into a shared FlyBuffer.
    auto buf = CMMakeShared<FlyBuffer>();
    buf->take(std::move(buffer));
    return buf;
}
