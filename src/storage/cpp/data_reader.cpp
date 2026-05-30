#include <storage/cpp/data_reader.h>
#include <log/cpp/logger.h>
#include <filesystem>
#include <stdexcept>

namespace fs = std::filesystem;

DataReader::DataReader(
    const CMString& base_path,
    const CMString& data_path,
    const CMString& writer_id
)
    : base_path_(base_path)
    , data_path_(data_path)
    , writer_id_(writer_id) {

    CMString read_dir = data_path_.empty() ? base_path_ : data_path_;
    CMString idx_path = base_path_ + "/" + writer_id_ + ".idx";

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
    if (!data_path_.empty()) {
        CMString local_path = data_path_ + "/" + file_name;
        if (fs::exists(local_path)) {
            return local_path;
        }
    }

    CMString base_path_file = base_path_ + "/" + file_name;
    if (fs::exists(base_path_file)) {
        return base_path_file;
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

CMString DataReader::read_raw_bytes(const CMString& object_name) {
    auto entry = index_->find_entry(object_name);
    if (!entry.has_value()) {
        ERR("read_raw_bytes: object not found: {}", object_name);
        return {};
    }
    return read_raw_bytes(entry.value());
}

CMString DataReader::read_raw_bytes(const IndexEntry& entry) {
    CMString file_path = find_file_path(entry.file_name);
    return read_from_file(file_path, entry.offset, entry.size);
}

CMString DataReader::read_from_file(const CMString& file_path, int64_t offset, int64_t size) {
    std::ifstream ifs(file_path, std::ios::binary);
    if (!ifs.is_open()) {
        ERR("Failed to open data file: {}", file_path);
        return {};
    }

    ifs.seekg(offset);
    CMString buffer(static_cast<size_t>(size), '\0');
    ifs.read(buffer.data(), static_cast<std::streamsize>(size));

    if (!ifs) {
        ERR("Failed to read data from file: {}", file_path);
        return {};
    }

    return buffer;
}
