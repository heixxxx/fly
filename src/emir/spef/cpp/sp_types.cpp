#include <emir/spef/cpp/sp_types.h>

#include <cerrno>
#include <cmath>
#include <cstdlib>

namespace fly {

// —— 词法公共单点实现（声明见 sp_types.h 头注；解析器与后续换算合并
//    层共用，禁止双份维护）——

bool sp_parse_number(const CMString& tok, double& v) {
    if (tok.empty()) {
        return false;
    }
    // 字符集预检：数字 / 小数点 / 科学计数 / 正负号——拒 inf/nan/十六
    // 进制等 strtod 也接受的形态（SPEF 数值不含这些）
    for (const char c : tok) {
        const bool legal =
            (c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' ||
            c == '+' || c == '-';
        if (!legal) {
            return false;
        }
    }
    char* endptr = nullptr;
    errno = 0;
    const double x = std::strtod(tok.c_str(), &endptr);
    // 只有溢出（结果 ±inf）判非法；下溢（如 1e-400，ERANGE 但结果为 0）
    // 按数值 0 收录——量级小到下溢的电容在物理上就是零，不该当破损
    if (endptr != tok.c_str() + tok.size() ||
        (errno == ERANGE && std::isinf(x))) {
        return false;
    }
    v = x;
    return true;
}

bool sp_parse_value(const CMString& tok, CMVector<double>& values) {
    values.clear();
    size_t begin = 0;
    for (;;) {
        const size_t colon = tok.find(':', begin);
        const CMString part =
            colon == CMString::npos ? tok.substr(begin)
                                    : tok.substr(begin, colon - begin);
        double v = 0.0;
        if (!sp_parse_number(part, v)) {
            return false;
        }
        values.push_back(v);
        if (colon == CMString::npos) {
            return true;
        }
        begin = colon + 1;
    }
}

uint32_t sp_default_value_index(uint32_t n) {
    return n == 3 ? 1u : 0u;   // 三值 min:typ:max 取典型值（裁定 6）
}

double sp_cap_unit_ff_per_unit(const CMString& unit_lower) {
    if (unit_lower == "pf") {
        return 1e3;   // 1 pF = 1000 fF
    }
    if (unit_lower == "ff") {
        return 1.0;
    }
    return 0.0;
}

double sp_res_unit_ohm_per_unit(const CMString& unit_lower) {
    if (unit_lower == "kohm") {
        return 1e3;
    }
    if (unit_lower == "ohm") {
        return 1.0;
    }
    return 0.0;
}

bool sp_time_unit_valid(const CMString& unit_lower) {
    return unit_lower == "ns" || unit_lower == "ps";
}

bool sp_ind_unit_valid(const CMString& unit_lower) {
    return unit_lower == "henry" || unit_lower == "mh" || unit_lower == "uh";
}

bool sp_delimiter_char_valid(char c) {
    return c == '.' || c == '/' || c == '|' || c == ':';
}

bool sp_bus_bracket_valid(char c) {
    return c == '[' || c == ']' || c == '{' || c == '}' || c == '(' ||
           c == ')' || c == '<' || c == '>';
}

bool sp_bus_pair_valid(char left, char right) {
    return (left == '[' && right == ']') || (left == '{' && right == '}') ||
           (left == '(' && right == ')') || (left == '<' && right == '>');
}

}  // namespace fly
