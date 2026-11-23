#include <emir/waveform/cpp/wf_types.h>

#include <cctype>
#include <utility>

namespace fly {

bool wf_var_kind_supported(WFVarKind kind) {
    return kind != WFVarKind::REAL && kind != WFVarKind::EVENT;
}

// —— 词法公共单点（裁定 IV-5）：解析器与块扫描器共用，实现自
//    wf_parser.cpp 双份词法收拢 ——

bool wf_scalar_value(char c, WFValue& out) {
    switch (c) {
        case '0':
            out = WFValue::ZERO;
            return true;
        case '1':
            out = WFValue::ONE;
            return true;
        case 'x':
        case 'X':
            out = WFValue::X;
            return true;
        case 'z':
        case 'Z':
            out = WFValue::Z;
            return true;
        default:
            return false;
    }
}

// 档位单位 → fs（s=1e15 / ms=1e12 / us=1e9 / ns=1e6 / ps=1e3 / fs=1）。
// 非法单位返回 0
uint64_t wf_timescale_unit_fs(const CMString& unit) {
    if (unit == "s") {
        return 1000000000000000ULL;
    }
    if (unit == "ms") {
        return 1000000000000ULL;
    }
    if (unit == "us") {
        return 1000000000ULL;
    }
    if (unit == "ns") {
        return 1000000ULL;
    }
    if (unit == "ps") {
        return 1000ULL;
    }
    if (unit == "fs") {
        return 1ULL;
    }
    return 0;
}

bool wf_parse_timescale(const CMString& joined, uint64_t& fs_out,
                        double& sec_out) {
    size_t i = 0;
    while (i < joined.size() && joined[i] == ' ') {
        ++i;
    }
    const size_t num_begin = i;
    while (i < joined.size() && joined[i] >= '0' && joined[i] <= '9') {
        ++i;
    }
    if (i == num_begin) {
        return false;
    }
    const CMString num = joined.substr(num_begin, i - num_begin);
    while (i < joined.size() && joined[i] == ' ') {
        ++i;
    }
    CMString unit;
    while (i < joined.size() &&
           std::isalpha(static_cast<unsigned char>(joined[i])) != 0) {
        unit += static_cast<char>(
            std::tolower(static_cast<unsigned char>(joined[i])));
        ++i;
    }
    if (i != joined.size() || unit.empty()) {
        return false;
    }
    uint64_t mult = 0;
    if (num == "1") {
        mult = 1;
    } else if (num == "10") {
        mult = 10;
    } else if (num == "100") {
        mult = 100;
    }
    const uint64_t unit_fs = wf_timescale_unit_fs(unit);
    if (mult == 0 || unit_fs == 0) {
        return false;
    }
    fs_out = mult * unit_fs;
    sec_out = static_cast<double>(fs_out) * 1e-15;
    return true;
}

bool wf_dump_keyword(const CMString& tok, CMString& kw, bool& args_follow) {
    kw = tok;
    args_follow = false;
    const size_t paren = kw.find('(');
    if (paren != CMString::npos) {
        kw.resize(paren);
        args_follow = tok.find(')', paren) == CMString::npos;
    }
    return kw == "$dumpvars" || kw == "$dumpall" || kw == "$dumpon" ||
           kw == "$dumpoff";
}

WFVarKind wf_parse_var_kind(const CMString& text) {
    // 小写归一后精确匹配（$var type 字段规范全小写；容忍大写变体）
    CMString lower;
    lower.reserve(text.size());
    for (char c : text) {
        lower += static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
    }
    if (lower == "wire") {
        return WFVarKind::WIRE;
    }
    if (lower == "reg") {
        return WFVarKind::REG;
    }
    if (lower == "tri" || lower == "tri0" || lower == "tri1") {
        return WFVarKind::TRI;
    }
    if (lower == "wand" || lower == "triand") {
        return WFVarKind::WAND;
    }
    if (lower == "wor" || lower == "trior") {
        return WFVarKind::WOR;
    }
    if (lower == "trireg") {
        return WFVarKind::TRIREG;
    }
    if (lower == "port") {
        return WFVarKind::PORT;
    }
    if (lower == "real" || lower == "realtime") {
        return WFVarKind::REAL;
    }
    if (lower == "event") {
        return WFVarKind::EVENT;
    }
    // integer/time/parameter 等可转储类型兜底（b 前缀向量语义）
    return WFVarKind::OTHER;
}

void WFVarTable::rebuild_indexes() {
    track_index_.clear();
    name_index_.clear();
    track_index_.reserve(track_ids_.size());
    name_index_.reserve(vars_.size());
    for (size_t i = 0; i < track_ids_.size(); ++i) {
        track_index_.emplace(track_ids_[i], i);
    }
    for (size_t i = 0; i < vars_.size(); ++i) {
        name_index_.emplace(vars_[i].name_, i);
    }
}

const WFVar* WFVarTable::find_by_name(const CMString& name) const {
    auto it = name_index_.find(name);
    return it == name_index_.end() ? nullptr : &vars_[it->second];
}

size_t WFVarTable::find_track(const CMString& id_code) const {
    auto it = track_index_.find(id_code);
    return it == track_index_.end() ? kNoTrack : it->second;
}

}  // namespace fly
