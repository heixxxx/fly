#include <storage/cpp/data_writer.h>
#include <log/cpp/logger.h>
#include <common/cpp/writer_id.h>
#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;

DataWriter::DataWriter(
    const CMString& base_path,
    const CMString& data_path,
    const CMString& writer_id,
    int64_t aggregation_threshold,
    const CMString& host
)
    : base_path_(base_path)
    , data_path_(data_path)
    , writer_id_(writer_id.empty() ? generate_writer_id() : writer_id)
    , host_(host)
    , aggregation_threshold_(aggregation_threshold) {

    fs::create_directories(base_path_);
    CMString write_dir = data_path_.empty() ? base_path_ : data_path_;
    fs::create_directories(write_dir);

    CMString idx_path = base_path_ + "/" + writer_id_ + ".idx";
    index_ = CMMakeUnique<LocalIndex>(idx_path);

    if (fs::exists(idx_path)) {
        index_->load();
    }

    while (true) {
        std::ostringstream oss;
        oss << "data_" << writer_id_ << "_" << std::setfill('0')
            << std::setw(3) << file_index_ << ".dat";
        CMString candidate = write_dir + "/" + oss.str();
        if (fs::exists(candidate)) {
            file_index_++;
        } else {
            break;
        }
    }

    create_new_file();
}

DataWriter::~DataWriter() {
    close();
}

void DataWriter::write_record(const CMString& object_name,
                               int64_t original_size,
                               int32_t chunk_count,
                               const FlyBuffer& record) {
    if (closed_) {
        ERR("DataWriter is closed"); return;
    }

    if (current_file_size_ + static_cast<int64_t>(record.size()) > aggregation_threshold_ && current_file_size_ > 0) {
        file_index_++;
        create_new_file();
    }

    int64_t offset = current_file_size_;

    file_stream_.write(record.data(), static_cast<std::streamsize>(record.size()));

    int64_t end_pos = file_stream_.tellp();
    int64_t entry_size = end_pos - offset;
    current_file_size_ = end_pos;

    IndexEntry entry{object_name, current_file_, offset, entry_size,
                      false, chunk_count, host_};
    index_->add_entry(entry);

    total_bytes_ += original_size;
}

void DataWriter::flush() {
    if (file_stream_.is_open()) {
        file_stream_.flush();
    }
    index_->save();
}

void DataWriter::close() {
    if (closed_) {
        return;
    }
    if (file_stream_.is_open()) {
        file_stream_.flush();
        file_stream_.close();
    }
    index_->save();
    closed_ = true;
}

int64_t DataWriter::total_bytes_written() const {
    return total_bytes_;
}

int32_t DataWriter::file_count() const {
    return file_index_;
}

IndexEntry* DataWriter::get_last_entry(const CMString& object_name) {
    return index_->find_entry(object_name);
}

CMVector<IndexEntry>* DataWriter::get_all_entries(const CMString& object_name) {
    return index_->find_all_entries(object_name);
}

bool DataWriter::remove_entry(const CMString& object_name) {
    return index_->remove_entry(object_name);
}

void DataWriter::create_new_file() {
    if (file_stream_.is_open()) {
        file_stream_.close();
    }

    current_file_ = get_current_file_name();
    CMString write_dir = data_path_.empty() ? base_path_ : data_path_;
    CMString file_path = write_dir + "/" + current_file_;

    fs::create_directories(write_dir);
    file_stream_.open(file_path, std::ios::binary);
    if (!file_stream_.is_open()) {
        ERR("Failed to create data file: {}", file_path);
        closed_ = true;
        return;
    }

    current_file_size_ = 0;
}

CMString DataWriter::get_current_file_name() {
    std::ostringstream oss;
    oss << "data_" << writer_id_ << "_" << std::setfill('0') << std::setw(3) << file_index_ << ".dat";
    return oss.str();
}
