#include <storage/cpp/decompressing_streambuf.h>
#include <serialization/cpp/object_header.h>
#include <cstring>

DecompressingStreamBuf::DecompressingStreamBuf(const char* data, size_t size)
    : chunk_data_(nullptr), chunk_data_size_(0) {
    if (!data || size == 0) return;

    size_t fixed_sz = static_cast<size_t>(ObjectHeader::fixed_header_size());
    if (size < fixed_sz) return;

    uint16_t py_name_len;
    std::memcpy(&py_name_len, data + sizeof(uint32_t) + sizeof(uint8_t), sizeof(uint16_t));

    size_t full_header_sz = fixed_sz + py_name_len;
    if (size < full_header_sz) return;

    CMString header_data(data, full_header_sz);
    int64_t offset = 0;
    ObjectHeader header = ObjectHeader::deserialize(header_data, offset);

    py_name_ = header.py_name;
    auto comp_type = static_cast<CompressionType>(header.compression_type);
    if (comp_type != CompressionType::NONE) {
        compressor_ = CompressorFactory::create(comp_type);
    }

    chunk_data_ = data + full_header_sz;
    chunk_data_size_ = size - full_header_sz;
    chunk_data_pos_ = 0;
    buffer_.reserve(4096);
}

DecompressingStreamBuf::~DecompressingStreamBuf() = default;

DecompressingStreamBuf::int_type DecompressingStreamBuf::underflow() {
    if (buffer_pos_ < buffer_avail_) {
        return traits_type::to_int_type(static_cast<unsigned char>(buffer_[buffer_pos_]));
    }
    if (!refill()) {
        return traits_type::eof();
    }
    return traits_type::to_int_type(static_cast<unsigned char>(buffer_[buffer_pos_]));
}

std::streamsize DecompressingStreamBuf::xsgetn(char* s, std::streamsize n) {
    std::streamsize copied = 0;
    while (copied < n) {
        if (buffer_pos_ >= buffer_avail_) {
            if (!refill()) break;
        }
        auto avail = std::min(
            static_cast<std::streamsize>(buffer_avail_ - buffer_pos_),
            n - copied);
        std::memcpy(s + copied, buffer_.data() + buffer_pos_, static_cast<size_t>(avail));
        buffer_pos_ += static_cast<size_t>(avail);
        copied += avail;
    }
    return copied;
}

bool DecompressingStreamBuf::refill() {
    if (chunk_data_pos_ >= chunk_data_size_) return false;

    constexpr size_t chunk_header_sz = sizeof(int32_t) * 2;
    if (chunk_data_pos_ + chunk_header_sz > chunk_data_size_) return false;

    int32_t uncomp_size, comp_size;
    std::memcpy(&uncomp_size, chunk_data_ + chunk_data_pos_, sizeof(int32_t));
    chunk_data_pos_ += sizeof(int32_t);
    std::memcpy(&comp_size, chunk_data_ + chunk_data_pos_, sizeof(int32_t));
    chunk_data_pos_ += sizeof(int32_t);

    if (chunk_data_pos_ + static_cast<size_t>(comp_size) > chunk_data_size_) return false;

    if (compressor_) {
        CMString comp_data(chunk_data_ + chunk_data_pos_, static_cast<size_t>(comp_size));
        chunk_data_pos_ += static_cast<size_t>(comp_size);
        CMString decompressed = compressor_->decompress(uncomp_size, comp_data);
        buffer_.assign(decompressed.begin(), decompressed.end());
    } else {
        buffer_.assign(
            chunk_data_ + chunk_data_pos_,
            chunk_data_ + chunk_data_pos_ + static_cast<size_t>(comp_size));
        chunk_data_pos_ += static_cast<size_t>(comp_size);
    }

    buffer_pos_ = 0;
    buffer_avail_ = buffer_.size();
    return !buffer_.empty();
}
