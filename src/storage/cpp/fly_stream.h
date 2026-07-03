#pragma once
#include <storage/cpp/compressing_streambuf.h>
#include <storage/cpp/decompressing_streambuf.h>
#include <storage/cpp/fly_buffer_stream.h>
#include <storage/cpp/compressor.h>
#include <common/cpp/fly_buffer.h>
#include <serialization/cpp/object_header.h>
#include <common/cpp/common_types.h>
#include <ostream>
#include <istream>

class FlyStream {
public:
    // compression_threshold: payloads at or below this size skip compression.
    // Defaults to 4096 to match Database; pass 0 to force compression of every
    // payload.
    FlyStream(CompressionType comp_type, int64_t chunk_size, const CMString& py_name = {},
              int64_t compression_threshold = 4096);
    explicit FlyStream(FlyBufferPtr data);
    ~FlyStream();
    FlyStream(const FlyStream&) = delete;
    FlyStream& operator=(const FlyStream&) = delete;

    void write(const char* data, size_t size);
    void flush();
    FlyBufferPtr finish_write();
    CMString read(size_t n);
    CMString read_all();
    CMString readline();
    size_t readinto(char* dst, size_t dst_size);
    int64_t total_uncompressed() const;
    int32_t chunk_count() const;
    bool is_write_mode() const { return is_write_mode_; }

private:
    bool is_write_mode_;
    FlyBufferPtr write_buf_;
    CMUniquePtr<FlyBufferStreamBuf> fly_buf_sb_;
    CMUniquePtr<CountingStreamBuf> counting_sb_;
    CMUniquePtr<std::ostream> counting_os_;
    CMUniquePtr<CompressingStreamBuf> compress_sb_;
    CMUniquePtr<std::ostream> compress_os_;
    CMString py_name_;
    FlyBufferPtr read_buf_;
    CMUniquePtr<DecompressingStreamBuf> decompress_sb_;
    CMUniquePtr<std::istream> decompress_is_;
};
