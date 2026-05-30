#pragma once

#include <common/cpp/common_types.h>

namespace fly {

// Decompress raw data (if compressed) and return decompressed result as a CMString.
// Handles both compressed and uncompressed data transparently.
CMString decompress_raw_data(const CMString& raw_data);

}  // namespace fly
