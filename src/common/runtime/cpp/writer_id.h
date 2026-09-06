#pragma once

#include <container/cpp/container_aliases.h>
#include <random>
#include <sstream>
#include <iomanip>

inline CMString generate_writer_id() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dist(0, 0xFFFFFFFF);
    uint32_t val = dist(gen);
    std::ostringstream oss;
    oss << std::hex << std::setfill('0') << std::setw(8) << val;
    return oss.str();
}
