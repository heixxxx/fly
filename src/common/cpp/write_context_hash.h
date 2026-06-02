#pragma once

#include <common/cpp/common_types.h>
#include <cstdint>
#include <sstream>
#include <iomanip>

inline CMString compute_write_context_hash(
    const CMString& task_name,
    const CMString& task_module,
    const CMVector<CMString>& args,
    const CMVector<CMString>& inputs)
{
    CMString combined;
    combined.reserve(256);
    combined += task_name;
    combined += '\0';
    combined += task_module;
    combined += '\0';
    for (const auto& a : args) { combined += a; combined += '\0'; }
    for (const auto& i : inputs) { combined += i; combined += '\0'; }

    uint64_t h1 = 0xcbf29ce484222325ULL;
    uint64_t h2 = 0x100000001b3ULL;
    for (char c : combined) {
        h1 ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
        h1 *= h2;
    }

    std::ostringstream oss;
    oss << std::hex << std::setfill('0')
        << std::setw(16) << h1
        << std::setw(16) << (h1 ^ (h1 >> 17));
    return oss.str();
}
