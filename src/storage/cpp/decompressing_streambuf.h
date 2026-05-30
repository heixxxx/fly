#pragma once

#include <storage/cpp/compressor.h>
#include <common/cpp/common_types.h>
#include <cstdint>
#include <memory>
#include <streambuf>
#include <vector>

// DecompressingStreamBuf — read-side counterpart of CompressingStreamBuf.
//
// Input format: [ObjectHeader][Chunk1][Chunk2]...
// Each chunk:   [int32_t uncompressed_size][int32_t compressed_size][data...]
//
// Decompresses chunks on demand, serving decompressed bytes via the
// std::streambuf interface. Designed to be paired with bitsery::InputStreamAdapter
// for zero-copy streaming deserialization:
//
//   DecompressingStreamBuf dsbuf(data, size);
//   std::istream is(&dsbuf);
//   FlyInputStreamAdapter adapter(is);
//   bitsery::quickDeserialization(std::move(adapter), obj);
//
// The input data pointer must outlive this object.
class DecompressingStreamBuf : public std::streambuf {
public:
    DecompressingStreamBuf(const char* data, size_t size);
    ~DecompressingStreamBuf() override;

    const CMString& py_name() const { return py_name_; }

protected:
    int_type underflow() override;
    std::streamsize xsgetn(char* s, std::streamsize n) override;

private:
    bool refill();

    const char* chunk_data_;
    size_t chunk_data_size_;
    size_t chunk_data_pos_ = 0;

    CMUniquePtr<Compressor> compressor_;
    CMString py_name_;

    CMVector<char> buffer_;
    size_t buffer_pos_ = 0;
    size_t buffer_avail_ = 0;
};
