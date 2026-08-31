#include <storage/cpp/compressing_streambuf.h>
#include <string_view>

// 流插件化（2026-08-31）：本类为 ostream/sink 适配薄壳——切块、压缩、
// CRC、块格式化均在 fly::WritePipeline 管线中；统计（total/chunk_count/
// 块表）由管线转发。流级 raw 阈值（旧"小对象跳过压缩"语义）经
// raw_threshold 传入 CompressStage；effective type 的 all-raw 判定见头文件。

CompressingStreamBuf::CompressingStreamBuf(std::ostream& dest,
                                           CMUniquePtr<Compressor> compressor,
                                           int64_t chunk_size,
                                           int64_t compression_threshold)
    : dest_(&dest)
    , comp_type_(compressor ? compressor->type() : CompressionType::NONE)
    , chunk_size_(chunk_size)
    , compression_threshold_(compression_threshold)
    , pipeline_(fly::make_file_write_pipeline(
          std::move(compressor), chunk_size,
          [this](const char* d, size_t n) { emit(d, n); },
          /*ratio_floor_pct*/ 85,
          /*raw_threshold*/ compression_threshold)) {}

CompressingStreamBuf::CompressingStreamBuf(
        CMUniquePtr<Compressor> compressor, int64_t chunk_size,
        std::function<void(const char*, size_t)> sink, int64_t compression_threshold)
    : sink_(std::move(sink))
    , comp_type_(compressor ? compressor->type() : CompressionType::NONE)
    , chunk_size_(chunk_size)
    , compression_threshold_(compression_threshold)
    , pipeline_(fly::make_file_write_pipeline(
          std::move(compressor), chunk_size,
          [this](const char* d, size_t n) { emit(d, n); },
          /*ratio_floor_pct*/ 85,
          /*raw_threshold*/ compression_threshold)) {}

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
        const char c = static_cast<char>(ch);
        pipeline_.write(&c, 1);
    }
    return ch;
}

std::streamsize CompressingStreamBuf::xsputn(const char* s, std::streamsize n) {
    pipeline_.write(s, static_cast<size_t>(n));
    return n;
}

int CompressingStreamBuf::sync() {
    pipeline_.finish();
    if (dest_) dest_->flush();
    return 0;
}
