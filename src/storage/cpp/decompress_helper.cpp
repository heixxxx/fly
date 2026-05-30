#include <storage/cpp/decompress_helper.h>
#include <storage/cpp/decompressing_streambuf.h>
#include <istream>

namespace fly {

CMString decompress_raw_data(const CMString& raw_data) {
    if (raw_data.empty()) return {};
    DecompressingStreamBuf dsbuf(raw_data.data(), raw_data.size());
    std::istream is(&dsbuf);
    CMString result;
    CMVector<char> tmp(4096);
    while (is) {
        is.read(tmp.data(), static_cast<std::streamsize>(tmp.size()));
        if (is.gcount() > 0) {
            result.append(tmp.data(), static_cast<size_t>(is.gcount()));
        }
    }
    return result;
}

}  // namespace fly
