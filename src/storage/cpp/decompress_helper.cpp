#include <storage/cpp/decompress_helper.h>
#include <storage/cpp/decompressing_streambuf.h>
#include <serialization/cpp/object_header.h>
#include <istream>

namespace fly {

CMString decompress_raw_data(const CMString& raw_data) {
    if (raw_data.empty()) return {};

    // Read expected decompressed size from ObjectHeader
    int64_t offset = 0;
    int64_t expected_size = 0;
    {
        ObjectHeader header;
        if (ObjectHeader::deserialize(raw_data, offset, header) && header.total_size_ > 0) {
            expected_size = static_cast<int64_t>(header.total_size_);
        }
        // If header parsing fails, use default size
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

    return result;
}

}  // namespace fly
