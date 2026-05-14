#pragma once

#include <storage/cpp/compressor.h>
#include <common/cpp/common_types.h>
#include <cstdint>
#include <fstream>

namespace compression_utils {

CMString serialize_chunk(const CompressedChunk& chunk);

CompressedChunk deserialize_chunk(const CMString& data, int64_t& offset);

int64_t write_compressed_to_stream(const CompressedChunk& chunk, std::ofstream& ofs);

CompressedChunk read_compressed_from_stream(std::ifstream& ifs, int64_t offset);

}