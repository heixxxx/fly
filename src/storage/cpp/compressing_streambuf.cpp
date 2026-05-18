#include <storage/cpp/compressing_streambuf.h>

CompressingStreamBuf::CompressingStreamBuf(std::ostream& dest,
                                           CMUniquePtr<Compressor> compressor,
                                           int64_t chunk_size)
    : dest_(dest)
    , compressor_(std::move(compressor))
    , chunk_size_(chunk_size) {
    buffer_.reserve(static_cast<size_t>(chunk_size));
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

int CompressingStreamBuf::sync() {
    if (!buffer_.empty()) {
        flush_chunk();
    }
    dest_.flush();
    return 0;
}

void CompressingStreamBuf::flush_chunk() {
    if (buffer_.empty()) {
        return;
    }

    total_uncompressed_ += static_cast<int64_t>(buffer_.size());

    CMString input(buffer_.begin(), buffer_.end());

    if (compressor_) {
        CompressedChunk chunk = compressor_->compress(input);

        int32_t uncomp_size = chunk.uncompressed_size;
        int32_t comp_size = chunk.compressed_size;

        dest_.write(reinterpret_cast<const char*>(&uncomp_size), sizeof(int32_t));
        dest_.write(reinterpret_cast<const char*>(&comp_size), sizeof(int32_t));
        dest_.write(chunk.data.data(), static_cast<std::streamsize>(chunk.compressed_size));
    } else {
        int32_t size = static_cast<int32_t>(buffer_.size());

        dest_.write(reinterpret_cast<const char*>(&size), sizeof(int32_t));
        dest_.write(reinterpret_cast<const char*>(&size), sizeof(int32_t));
        dest_.write(buffer_.data(), static_cast<std::streamsize>(buffer_.size()));
    }

    chunk_count_++;
    buffer_.clear();
}