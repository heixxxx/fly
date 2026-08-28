#include <storage/cpp/decompress_helper.h>
#include <storage/cpp/decompressing_streambuf.h>
#include <serialization/cpp/object_header.h>
#include <istream>
#include <stdexcept>

namespace fly {

// 解压压缩 record（trailer 格式，§4.4）。任一校验失败（trailer/块 CRC/结构
// 越界/解压错误）抛 std::runtime_error("[FATAL-DATA-CORRUPTION] ...")——
// 调用方（Python 导出面/worker 内部任务）按零容忍语义处理，不得消费截断数据。
CMString decompress_raw_data(const CMString& raw_data) {
    if (raw_data.empty()) return {};

    // Read expected decompressed size from the tail trailer
    int64_t expected_size = 0;
    {
        ObjectHeader header;
        size_t trailer_len = 0;
        if (ObjectHeader::deserialize_trailer(raw_data, header, trailer_len) &&
            header.total_size_ > 0) {
            expected_size = static_cast<int64_t>(header.total_size_);
        }
        // If trailer parsing fails, use default size（fallback 路径读到底，
        // 校验状态由 dsbuf.checksum_failed() 统一裁决）
    }

    DecompressingStreamBuf dsbuf(raw_data.data(), raw_data.size());
    std::istream is(&dsbuf);

    CMString result;

    if (expected_size > 0) {
        // Pre-allocate exact size - no reallocations needed
        result.resize(static_cast<size_t>(expected_size));
        is.read(result.data(), expected_size);
        auto gcount = is.gcount();
        if (gcount > 0 && gcount < expected_size) {
            result.resize(static_cast<size_t>(gcount));
        }
    } else {
        // Fallback: read with doubling strategy
        result.resize(65536);  // 64KB initial
        size_t pos = 0;

        while (is) {
            if (pos >= result.size()) {
                result.resize(result.size() * 2);
            }

            is.read(result.data() + pos, static_cast<std::streamsize>(result.size() - pos));
            auto gcount = is.gcount();
            if (gcount > 0) {
                pos += static_cast<size_t>(gcount);
            }
        }

        result.resize(pos);
    }

    if (dsbuf.checksum_failed()) {
        throw std::runtime_error("[FATAL-DATA-CORRUPTION] decompress_raw_data: chunk CRC/trailer verify failed");
    }

    return result;
}

}  // namespace fly
