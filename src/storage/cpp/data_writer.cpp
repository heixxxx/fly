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
                                const FlyBuffer& record,
                                const CMString& write_context_hash) {
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
                      false, chunk_count, host_, write_context_hash};
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

void DataWriter::mark_begin() {
    // 记录当前 data 文件位置作为回滚点，供 ABORT 时 truncate。
    segment_point_.file_index_ = file_index_;
    segment_point_.file_offset_ = current_file_size_;
    segment_point_.active_ = true;
    index_->mark_begin();
}

void DataWriter::mark_end() {
    segment_point_.active_ = false;
    index_->mark_end();
}

void DataWriter::abort_segment() {
    if (!segment_point_.active_) {
        // 本 db 本 task 无写入（段未开），no-op。
        return;
    }

    // 先 flush，保证当前缓冲区内的脏字节落盘，truncate 才能精确截断。
    if (file_stream_.is_open()) {
        file_stream_.flush();
    }
    // idx 打 ABORT：整段 pending 撤销，无需逐个 REMOVE。
    index_->mark_abort();

    // data 文件 truncate 回回滚点，回收脏字节的磁盘占用。
    rollback_data_file();

    segment_point_.active_ = false;
}

void DataWriter::rollback_data_file() {
    // 关闭当前流，准备 truncate。
    if (file_stream_.is_open()) {
        file_stream_.close();
    }

    CMString write_dir = data_path_.empty() ? base_path_ : data_path_;

    // 情况1：BEGIN 后发生过 rollover（本 task 写出的 .dat 超过 1 个）。
    //   删除回滚点之后新创建的所有 .dat（file_index_ 递减回回滚点序号）。
    while (file_index_ > segment_point_.file_index_) {
        CMString path = write_dir + "/" + get_file_name(file_index_);
        std::error_code ec;
        fs::remove(path, ec);   // 忽略不存在
        file_index_--;
    }

    // 情况2（或情况1的收尾）：截断回滚点 .dat 到 BEGIN 时记录的偏移。
    CMString rollback_file = write_dir + "/" + get_file_name(segment_point_.file_index_);
    std::error_code ec;
    if (segment_point_.file_offset_ == 0) {
        // 回滚点是文件开头：直接清空文件（resize_file 到 0）。
        fs::resize_file(rollback_file, 0, ec);
    } else {
        fs::resize_file(rollback_file, static_cast<uintmax_t>(segment_point_.file_offset_), ec);
    }
    if (ec) {
        ERR("Failed to truncate data file {}: {}", rollback_file, ec.message());
    }

    // 重新打开该文件继续追加，重置内存位置游标。
    current_file_ = get_file_name(segment_point_.file_index_);
    current_file_size_ = segment_point_.file_offset_;
    file_stream_.open(rollback_file, std::ios::binary | std::ios::app);
    if (!file_stream_.is_open()) {
        ERR("Failed to reopen data file after rollback: {}", rollback_file);
    }
}

int64_t DataWriter::total_bytes_written() const {
    return total_bytes_;
}

int32_t DataWriter::file_count() const {
    return file_index_;
}

std::optional<IndexEntry> DataWriter::get_last_entry(const CMString& object_name) {
    return index_->find_entry(object_name);
}

std::optional<CMVector<IndexEntry>> DataWriter::get_all_entries(const CMString& object_name) {
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
    return get_file_name(file_index_);
}

CMString DataWriter::get_file_name(int32_t index) const {
    std::ostringstream oss;
    oss << "data_" << writer_id_ << "_" << std::setfill('0')
        << std::setw(3) << index << ".dat";
    return oss.str();
}
