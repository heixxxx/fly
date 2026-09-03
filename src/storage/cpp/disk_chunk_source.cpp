#include <storage/cpp/disk_chunk_source.h>
#include <log/cpp/logger.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

namespace fly {

DiskChunkSource::DiskChunkSource(CMString file_path, uint64_t offset, uint64_t size,
                                 CMString py_name, uint64_t total_uncompressed,
                                 uint32_t chunk_count, int comp_type)
    : file_path_(std::move(file_path))
    , offset_(offset)
    , size_(size)
    , py_name_(std::move(py_name))
    , total_uncompressed_(total_uncompressed)
    , chunk_count_(chunk_count)
    , comp_type_(comp_type) {
    fd_ = ::open(file_path_.c_str(), O_RDONLY);
    if (fd_ < 0) {
        ERR("[DISK-SRC] open failed: {}", file_path_);
        failure_detail_ = "io: open failed errno=" + std::to_string(errno) + " file=" + file_path_;
        failed_ = true;
    }
}

DiskChunkSource::~DiskChunkSource() {
    if (fd_ >= 0) ::close(fd_);
}

int64_t DiskChunkSource::pull(char* dst, size_t n) {
    if (failed_) return -1;
    uint64_t remain = size_ > pos_ ? size_ - pos_ : 0;
    size_t take = static_cast<size_t>(n < remain ? n : remain);
    if (take == 0) return 0;  // record 区间拉尽

    ssize_t got = ::pread(fd_, dst, take, static_cast<off_t>(offset_ + pos_));
    if (got < 0) {
        ERR("[DISK-SRC] pread failed: file={} pos={}", file_path_, pos_);
        failure_detail_ = "io: pread failed errno=" + std::to_string(errno)
                          + " file=" + file_path_ + " pos=" + std::to_string(pos_);
        failed_ = true;
        return -1;
    }
    if (static_cast<size_t>(got) < take) {
        // 短读 = 文件被截断/区间越界 = 结构损坏（完整性，非 IO）。
        ERR("[DISK-SRC] short read: file={} want={} got={}", file_path_, take, got);
        failure_detail_ = "integrity: short read (record truncated) file=" + file_path_;
        failed_ = true;
        return -1;
    }
    pos_ += take;
    return static_cast<int64_t>(take);
}

}  // namespace fly
