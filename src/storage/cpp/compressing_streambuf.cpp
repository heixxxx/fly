#include <storage/cpp/compressing_streambuf.h>
#include <common/cpp/data_checksum.h>
#include <string_view>

CompressingStreamBuf::CompressingStreamBuf(std::ostream& dest,
                                           CMUniquePtr<Compressor> compressor,
                                           int64_t chunk_size,
                                           int64_t compression_threshold)
    : dest_(&dest)
    , compressor_(std::move(compressor))
    , chunk_size_(chunk_size)
    , compression_threshold_(compression_threshold) {
    buffer_.reserve(static_cast<size_t>(chunk_size));
}

CompressingStreamBuf::CompressingStreamBuf(
        CMUniquePtr<Compressor> compressor, int64_t chunk_size,
        std::function<void(const char*, size_t)> sink, int64_t compression_threshold)
    : sink_(std::move(sink))
    , compressor_(std::move(compressor))
    , chunk_size_(chunk_size)
    , compression_threshold_(compression_threshold) {
    buffer_.reserve(static_cast<size_t>(chunk_size));
}

void CompressingStreamBuf::emit(const char* data, size_t n) {
    if (sink_) {
        sink_(data, n);
    } else if (dest_) {
        dest_->write(data, static_cast<std::streamsize>(n));
    }
}

CompressingStreamBuf::~CompressingStreamBuf() {
    try {
        sync();
    } catch (...) {
    }
}

CompressingStreamBuf::int_type CompressingStreamBuf::overflow(int_type ch) {
    if (ch != traits_type::eof()) {
        buffer_.push_back(static_cast<char>(ch));
        if (static_cast<int64_t>(buffer_.size()) >= chunk_size_) {
            flush_chunk();
        }
    }
    return ch;
}

std::streamsize CompressingStreamBuf::xsputn(const char* s, std::streamsize n) {
    std::streamsize written = 0;
    while (written < n) {
        // Flush first if buffer is already at/over capacity, so the space
        // computation below is always positive and written always advances.
        if (static_cast<int64_t>(buffer_.size()) >= chunk_size_) {
            flush_chunk();
        }
        auto space = chunk_size_ - static_cast<int64_t>(buffer_.size());
        auto to_write = std::min(static_cast<std::streamsize>(space), n - written);
        buffer_.insert(buffer_.end(), s + written, s + written + to_write);
        written += to_write;
    }
    return written;
}

int CompressingStreamBuf::sync() {
    if (!buffer_.empty()) {
        flush_chunk();
    }
    if (dest_) dest_->flush();
    return 0;
}

void CompressingStreamBuf::flush_chunk() {
    if (buffer_.empty()) {
        return;
    }

    total_uncompressed_ += static_cast<int64_t>(buffer_.size());

    // Small-object fast path: if a compressor is configured but the buffered
    // payload is at or below the threshold, skip compression entirely and write
    // the bytes raw. The decision is made on the first chunk.
    //
    // Safety constraint: skip is only allowed when compression_threshold_ <
    // chunk_size_. Proof: an under-full buffer (size < chunk_size_) is always
    // the final chunk, so if the *first* chunk is under-full (size <= threshold
    // < chunk_size_) the whole payload fit in one chunk — no mixing of raw and
    // compressed chunks. Without this guard (threshold >= chunk_size), the
    // first full chunk (size == chunk_size <= threshold) would be written raw
    // while any following chunks would compress, producing a mixed-format
    // record that the read-side cannot decode. Sets skipped_ so callers can
    // correct the ObjectHeader.compression_type_ to NONE for read-back.
    bool skip = compressor_ && chunk_count_ == 0 &&
                compression_threshold_ < chunk_size_ &&
                static_cast<int64_t>(buffer_.size()) <= compression_threshold_;
    if (skip) {
        skipped_ = true;
    }

    // Zero-copy: use string_view to avoid constructing CMString
    std::string_view input(buffer_.data(), buffer_.size());

    // 块格式（chunked-transfer-design.md §4.4）：
    //   [i32 unc][i32 comp][u64 crc][data]，crc = data_checksum(压缩后字节)
    // crc 是写入时刻锚点，覆盖 record 全生命周期（磁盘 → server → 网络 →
    // client → 解压）。compressed 与 raw passthrough 分支同格式。
    if (compressor_ && !skip) {
        CompressedChunk chunk = compressor_->compress(input);

        int32_t uncomp_size = chunk.uncompressed_size_;
        int32_t comp_size = chunk.compressed_size_;
        uint64_t crc = fly::data_checksum(chunk.data_.data(),
                                          static_cast<size_t>(chunk.compressed_size_));

        block_comp_lens_.push_back(static_cast<uint32_t>(comp_size));
        emit(reinterpret_cast<const char*>(&uncomp_size), sizeof(int32_t));
        emit(reinterpret_cast<const char*>(&comp_size), sizeof(int32_t));
        emit(reinterpret_cast<const char*>(&crc), sizeof(uint64_t));
        emit(chunk.data_.data(), static_cast<size_t>(chunk.compressed_size_));
    } else {
        int32_t size = static_cast<int32_t>(buffer_.size());
        uint64_t crc = fly::data_checksum(buffer_.data(), buffer_.size());

        block_comp_lens_.push_back(static_cast<uint32_t>(size));
        emit(reinterpret_cast<const char*>(&size), sizeof(int32_t));
        emit(reinterpret_cast<const char*>(&size), sizeof(int32_t));
        emit(reinterpret_cast<const char*>(&crc), sizeof(uint64_t));
        emit(buffer_.data(), buffer_.size());
    }

    chunk_count_++;
    buffer_.clear();
}
