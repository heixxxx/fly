#pragma once

#include <common/cpp/common_types.h>
#include <cstdint>
#include <chrono>
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

// 为裸写入（非 @as_task 路径，无 task context）生成唯一 write_context_hash。
// 用 system_clock 纳秒 + thread_local 递增计数器组合，保证同线程内每次调用必不同
// （即使系统时钟精度不足导致连续调用落到同一纳秒，counter 仍递增）。输出格式与
// compute_write_context_hash 一致（32 hex），使下游（idx entry / write_provenance_）
// 无差别消费。
inline CMString make_timestamp_hash() {
    auto now = std::chrono::system_clock::now().time_since_epoch();
    uint64_t ns = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
    static thread_local uint64_t counter = 0;
    uint64_t seq = counter++;
    uint64_t h1 = ns ^ (seq * 0x9e3779b97f4a7c15ULL);
    std::ostringstream oss;
    oss << std::hex << std::setfill('0')
        << std::setw(16) << h1
        << std::setw(16) << (h1 ^ (h1 >> 17));
    return oss.str();
}
