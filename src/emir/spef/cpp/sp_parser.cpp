#include <emir/spef/cpp/sp_parser.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <utility>

namespace fly {
namespace {

// 切块扫描/头部解析/网段解析共用的「无网段」哨兵偏移
constexpr uint64_t kNoOffset = UINT64_MAX;

// —— 词法层：token = 空白分隔的裸原子 + 引号串；注释（// 与 /* */）
//    在词法层透明跳过（注释里的关键字不算数——规格 3.6），块注释未
//    闭合属文件级错误。token 记录起始偏移与行首标记：网段关键字
//   （*D_NET 等）只在行首生效（规格 3.5 唯一的行级约定），是切块扫描
//    与网段识别共用的判定材料。——
struct SpToken {
    CMString text_;
    size_t start_ = 0;    // token 起始字节偏移（块界判定用）
    size_t line_ = 1;     // token 起始行（报错定位用）
    // 行首标记——仅供切块扫描的块边界判定（sp_plan_file_chunks 只把行
    // 首网段关键字当块界）。语义：本行此前仅空白与注释（注释对行首透
    // 明——同一行注释后面的网段关键字仍算行首；注释内的换行照常开新
    // 行）。网段识别不依赖本标记（解析器按 token 识别网段关键字，位置
    // 无关——避免行中关键字前有杂料时整网被无声丢弃）
    bool line_start_ = false;
    bool quoted_ = false;      // 引号串（text_ = 引号内原文）
};

class SpLexer {
public:
    void reset(const char* data, size_t size) {
        data_ = data;
        size_ = size;
        pos_ = 0;
        line_ = 1;
        at_line_start_ = true;
        has_pending_ = false;
    }

    // 读取下一 token；流尽返回 false
    bool next(SpToken& tok) {
        if (has_pending_) {
            tok = std::move(pending_);
            has_pending_ = false;
            return true;
        }
        tok = SpToken{};
        for (;;) {
            while (pos_ < size_ && is_space(data_[pos_])) {
                if (data_[pos_] == '\n') {
                    ++line_;
                    at_line_start_ = true;
                }
                ++pos_;
            }
            if (pos_ >= size_) {
                return false;
            }
            if (data_[pos_] == '/' && pos_ + 1 < size_ &&
                data_[pos_ + 1] == '/') {
                skip_line_comment();
                continue;
            }
            if (data_[pos_] == '/' && pos_ + 1 < size_ &&
                data_[pos_ + 1] == '*') {
                skip_block_comment();
                continue;
            }
            break;
        }
        tok.start_ = pos_;
        tok.line_ = line_;
        tok.line_start_ = at_line_start_;
        if (data_[pos_] == '"') {
            ++pos_;   // 开引号不入文
            while (pos_ < size_ && data_[pos_] != '"') {
                if (data_[pos_] == '\n') {
                    ++line_;   // 跨行引号串照常累加行号（报错定位准确）
                }
                tok.text_.push_back(data_[pos_]);
                ++pos_;
            }
            if (pos_ >= size_) {
                throw std::runtime_error("SPEF: unterminated string at line " +
                                         std::to_string(tok.line_));
            }
            ++pos_;   // 闭引号
            tok.quoted_ = true;
        } else {
            while (pos_ < size_ && !is_space(data_[pos_]) &&
                   data_[pos_] != '"' && !starts_comment(pos_)) {
                tok.text_.push_back(data_[pos_]);
                ++pos_;
            }
        }
        at_line_start_ = false;
        return true;
    }

    // 单槽回退（网段关键字 / 下一段关键字判定后的续起）
    void push_back(const SpToken& tok) {
        pending_ = tok;
        has_pending_ = true;
    }

private:
    static bool is_space(char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' ||
               c == '\v';
    }

    bool starts_comment(size_t pos) const {
        return data_[pos] == '/' && pos + 1 < size_ &&
               (data_[pos + 1] == '/' || data_[pos + 1] == '*');
    }

    // 行注释：消费到行尾换行前（换行留给空白跳过——保持行首判定）
    void skip_line_comment() {
        while (pos_ < size_ && data_[pos_] != '\n') {
            ++pos_;
        }
    }

    // 块注释：消费到 */。注释对网段识别与行首标记一律透明（同行/跨行
    // 注释都不改变网段关键字的识别），注释内的换行照常计行——行首标
    // 记仅供切块扫描（见 SpToken::line_start_）。未闭合 = 文件级错误
    //（规格 3.7）
    void skip_block_comment() {
        const size_t opened_line = line_;
        pos_ += 2;
        while (pos_ < size_) {
            if (data_[pos_] == '\n') {
                ++line_;
                at_line_start_ = true;
            } else if (data_[pos_] == '*' && pos_ + 1 < size_ &&
                       data_[pos_ + 1] == '/') {
                pos_ += 2;
                return;
            }
            ++pos_;
        }
        throw std::runtime_error(
            "SPEF: unterminated block comment (opened at line " +
            std::to_string(opened_line) + ")");
    }

    const char* data_ = nullptr;
    size_t size_ = 0;
    size_t pos_ = 0;
    size_t line_ = 1;
    bool at_line_start_ = true;
    SpToken pending_;
    bool has_pending_ = false;
};

// —— token 形态判定（头部解析 / 网段解析 / 切块扫描共用）——

// 关键字形态：非引号且 '*' 起头（'*' + 非全数字 = 关键字；'*' + 全数字
// = 名字映射引用）
bool is_keyword_form(const SpToken& t) {
    return !t.quoted_ && !t.text_.empty() && t.text_[0] == '*';
}

bool is_all_digits(const CMString& s) {
    if (s.empty()) {
        return false;
    }
    for (const char c : s) {
        if (c < '0' || c > '9') {
            return false;
        }
    }
    return true;
}

// 名字映射引用形态：*<正整数>
bool is_map_ref_form(const SpToken& t) {
    return !t.quoted_ && t.text_.size() >= 2 && t.text_[0] == '*' &&
           is_all_digits(t.text_.substr(1));
}

// 真关键字：'*' 起头且 '*' 后首字符非数字——数据位置（网名/节点/端口
// 名）的 *<正整数> 与 *<正整数>:<引脚> 都是引用不是关键字，判定必须走
// 本谓词。规格 3.4：引用与关键字同以 '*' 起头，「* 后数字 = 引用数据、
// '* 后字母 = 关键字」是词法的关键分叉（OpenSTA SpefLex 同口径）
bool is_keyword_tok(const SpToken& t) {
    return is_keyword_form(t) &&
           (t.text_.size() < 2 ||
            t.text_[1] < '0' || t.text_[1] > '9');
}

// 网段关键字（四种形态；生效还需 token 行首——调用方判定）
bool is_net_keyword_text(const CMString& text) {
    return text == "*D_NET" || text == "*R_NET" || text == "*D_PNET" ||
           text == "*R_PNET";
}

CMString to_lower(const CMString& s) {
    CMString out;
    out.reserve(s.size());
    for (const char c : s) {
        out.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c);
    }
    return out;
}

[[noreturn]] void spef_error(const CMString& what, size_t line,
                             const CMString& source) {
    throw std::runtime_error("SPEF: " + what + " at line " +
                             std::to_string(line) + ": " + source);
}

// 名字映射引用展开：*<正整数> -> 原名；未登记（或索引超出登记域）=
// false（裁定 11 的「未登记」，处置由调用方定）
bool expand_ref(const CMString& tok, const SPFileHeader& header,
                CMString& name_out) {
    const CMString digits = tok.substr(1);
    if (digits.size() > 10) {
        return false;   // 必超 uint32 登记域——按未登记处理
    }
    char* endptr = nullptr;
    const unsigned long long idx = std::strtoull(digits.c_str(), &endptr, 10);
    // 索引 0 按未登记兜底（返回 false → 记录丢弃/网跳过），不属语法破
    // 损：引用形态本身合法，0 只是值非法——登记表里不可能有索引 0
    //（定义处见 HeaderParser::parse_name_map，`*0 name` 是文件级错误），
    // 与「超出登记域」同理
    if (endptr != digits.c_str() + digits.size() || idx == 0 ||
        idx > 0xFFFFFFFFULL) {
        return false;
    }
    const auto it = header.name_map_.find(static_cast<uint32_t>(idx));
    if (it == header.name_map_.end()) {
        return false;
    }
    name_out = it->second;
    return true;
}

// 乘换算系数并归负零（存储值不得出现负零）：-0.0 字面量是合法数值
//（-0.0 < 0.0 为假，正常收录），乘系数后仍是 -0.0——归成 0.0
double scaled_nonneg_cap(double file_value, double scale) {
    const double ff = file_value * scale;
    return ff == 0.0 ? 0.0 : ff;
}

// 读一个数值 token（真关键字形态或非数值 = false）。读到真关键字时
// 先放回流再返回 false——调用方的段级破损跳扫（skip_to_next_net）必
// 须能命中该关键字，否则紧随的网段被整体跳过（无声丢网）
bool next_number(SpLexer& lex, SpToken& tok) {
    if (!lex.next(tok)) {
        return false;   // 流尽：无 token 可放回
    }
    if (is_keyword_tok(tok)) {
        lex.push_back(tok);
        return false;
    }
    double dummy = 0.0;
    return sp_parse_number(tok.text_, dummy);
}

// 读一个非关键字 token（名字/节点等；映射引用形态是数据不是关键字。
// 流尽 = false；真关键字 = 放回后 false——放回理由同 next_number）
bool next_plain_token(SpLexer& lex, SpToken& tok) {
    if (!lex.next(tok)) {
        return false;
    }
    if (is_keyword_tok(tok)) {
        lex.push_back(tok);
        return false;
    }
    return true;
}

// —— 文件头解析（sp_parse_header_text / *_file / 切块扫描共用同一
//    套文件级校验——规格 3.8 要求切块扫描照 3.7 抛文件级错误，禁止
//    两份校验逻辑）——
class HeaderParser {
public:
    HeaderParser(SPFileHeader& header, const CMString& source)
        : header_(header), source_(source) {}

    // 解析至流尽或首个行首网段关键字（该关键字回退不消费）；返回其
    // 字节偏移（无网段 = kNoOffset）
    uint64_t parse(SpLexer& lex) {
        lex_ = &lex;
        SpToken tok;
        if (!lex_->next(tok) || tok.quoted_ || tok.text_ != "*SPEF") {
            spef_error("first non-comment line is not *SPEF", tok.line_,
                       source_);
        }
        // 版本串（可选一个 token；原文保留——大小写不敏感仅指可解析）
        if (lex_->next(tok)) {
            if (is_keyword_form(tok)) {
                lex_->push_back(tok);
            } else {
                header_.version_ = tok.text_;
            }
        }
        for (;;) {
            if (!lex_->next(tok)) {
                break;
            }
            // 头段终点 = 网段关键字（按 token 识别、位置无关——行中关
            // 键字同样结束头部；quoted 串不是关键字）
            if (is_keyword_form(tok) && is_net_keyword_text(tok.text_)) {
                require_units_or_throw(tok.line_);
                lex_->push_back(tok);
                return tok.start_;
            }
            if (!is_keyword_form(tok)) {
                continue;   // 头部散料 token：静默跳过（版本串周边本就容
                            // 忍；无丢网风险——网关键字上一分支已截获，
                            // 故不计 stray，见 SPChunkResult 口径注释）
            }
            dispatch(tok);
        }
        require_units_or_throw(0);
        return kNoOffset;
    }

private:
    void dispatch(const SpToken& tok) {
        const CMString& kw = tok.text_;
        if (kw == "*DESIGN") {
            SpToken t;
            if (lex_->next(t) && !is_keyword_form(t)) {
                header_.design_name_ = t.text_;
            } else if (!t.text_.empty() && is_keyword_form(t)) {
                lex_->push_back(t);
            }
        } else if (kw == "*DATE" || kw == "*VENDOR" || kw == "*PROGRAM" ||
                   kw == "*VERSION") {
            SpToken t;
            if (lex_->next(t)) {
                if (is_keyword_form(t)) {
                    lex_->push_back(t);
                }
            }
        } else if (kw == "*DESIGN_FLOW") {
            collect_strings(header_.design_flow_);
        } else if (kw == "*DIVIDER" || kw == "*DELIMITER") {
            parse_delimiter(tok);
        } else if (kw == "*BUS_DELIMITER") {
            parse_bus_delimiter(tok);
        } else if (kw == "*T_UNIT" || kw == "*C_UNIT" || kw == "*R_UNIT" ||
                   kw == "*L_UNIT") {
            parse_unit(tok);
        } else if (kw == "*NAME_MAP") {
            parse_name_map(tok);
        } else if (kw == "*POWER_NETS") {
            collect_names(header_.power_nets_);
        } else if (kw == "*GROUND_NETS") {
            collect_names(header_.ground_nets_);
        } else if (kw == "*PORTS" || kw == "*PHYSICAL_PORTS" ||
                   kw == "*DEFINE" || kw == "*PDEFINE") {
            // 次要构造（裁定 12）：接受并忽略段内容
            collect_strings(discarded_);
        }
        // 其余未知关键字：跳过该 token（文件级错误清单不含此项——
        // 头部容忍；散落的参数 token 走杂料跳过）
    }

    // 连续收集非关键字 token（引号串或裸 token）至段结束
    void collect_strings(CMVector<CMString>& out) {
        for (;;) {
            SpToken t;
            if (!lex_->next(t)) {
                return;
            }
            if (is_keyword_form(t)) {
                lex_->push_back(t);
                return;
            }
            out.push_back(t.text_);
        }
    }

    void collect_names(CMVector<CMString>& out) { collect_strings(out); }

    void parse_delimiter(const SpToken& kw_tok) {
        SpToken t;
        if (!lex_->next(t)) {
            spef_error(CMString(kw_tok.text_) + " missing value", kw_tok.line_,
                       source_);
        }
        if (t.quoted_ || t.text_.size() != 1 ||
            !sp_delimiter_char_valid(t.text_[0])) {
            spef_error("invalid " + kw_tok.text_ + " value: " + t.text_,
                       t.line_, source_);
        }
        if (kw_tok.text_ == "*DIVIDER") {
            header_.divider_ = t.text_[0];
        } else {
            header_.delimiter_ = t.text_[0];
        }
    }

    // *BUS_DELIMITER：一到两个字符——单 token 双字符（"[]"）或两 token
    // 各一字符（"[ ]"）或单字符；字符须属合法括号对集合（规格 3.4）
    void parse_bus_delimiter(const SpToken& kw_tok) {
        SpToken t;
        if (!lex_->next(t)) {
            spef_error("*BUS_DELIMITER missing value", kw_tok.line_, source_);
        }
        char left = '\0';
        char right = '\0';
        if (t.quoted_) {
            spef_error("invalid *BUS_DELIMITER value: " + t.text_, t.line_,
                       source_);
        }
        if (t.text_.size() == 2) {
            left = t.text_[0];
            right = t.text_[1];
        } else if (t.text_.size() == 1) {
            left = t.text_[0];
            SpToken t2;
            if (lex_->next(t2)) {
                if (!t2.quoted_ && t2.text_.size() == 1 &&
                    sp_bus_bracket_valid(t2.text_[0])) {
                    right = t2.text_[0];
                } else {
                    lex_->push_back(t2);
                }
            }
        } else {
            spef_error("invalid *BUS_DELIMITER value: " + t.text_, t.line_,
                       source_);
        }
        if (!sp_bus_bracket_valid(left) ||
            (right != '\0' && !sp_bus_pair_valid(left, right))) {
            spef_error("invalid *BUS_DELIMITER value: " + t.text_ +
                           (right != '\0' ? CMString(" ") + right : ""),
                       t.line_, source_);
        }
        header_.bus_left_ = left;
        header_.bus_right_ = right;
    }

    // 四个单位行：「刻度 单位词」——刻度正实数（可小数）、单位词大小
    // 写不敏感且须在该单位行的合法集合（规格 3.2；缺单位词/非法 =
    // 文件级错误）
    void parse_unit(const SpToken& kw_tok) {
        SpToken t;
        if (!lex_->next(t)) {
            spef_error(kw_tok.text_ + " missing scale", kw_tok.line_, source_);
        }
        CMVector<double> scale_vals;
        if (t.quoted_ || !sp_parse_value(t.text_, scale_vals) ||
            scale_vals.size() != 1 || scale_vals[0] <= 0.0) {
            spef_error("invalid " + kw_tok.text_ + " scale: " + t.text_,
                       t.line_, source_);
        }
        const double scale = scale_vals[0];
        SpToken u;
        if (!lex_->next(u)) {
            spef_error(kw_tok.text_ + " missing unit word", kw_tok.line_,
                       source_);
        }
        const CMString lower = to_lower(u.text_);
        const CMString decl = t.text_ + " " + u.text_;
        if (kw_tok.text_ == "*C_UNIT") {
            const double ff = sp_cap_unit_ff_per_unit(lower);
            if (u.quoted_ || ff <= 0.0) {
                spef_error("unknown capacitance unit word: " + u.text_,
                           u.line_, source_);
            }
            header_.cap_scale_ff_ = scale * ff;
            header_.cap_unit_decl_ = decl;
            has_cap_unit_ = true;
        } else if (kw_tok.text_ == "*R_UNIT") {
            const double ohm = sp_res_unit_ohm_per_unit(lower);
            if (u.quoted_ || ohm <= 0.0) {
                spef_error("unknown resistance unit word: " + u.text_,
                           u.line_, source_);
            }
            header_.res_scale_ohm_ = scale * ohm;
            header_.res_unit_decl_ = decl;
            has_res_unit_ = true;
        } else if (kw_tok.text_ == "*T_UNIT") {
            if (u.quoted_ || !sp_time_unit_valid(lower)) {
                spef_error("unknown time unit word: " + u.text_, u.line_,
                           source_);
            }
            header_.time_unit_decl_ = decl;   // 首版不消费，记录原声明
        } else {
            if (u.quoted_ || !sp_ind_unit_valid(lower)) {
                spef_error("unknown inductance unit word: " + u.text_,
                           u.line_, source_);
            }
            header_.ind_unit_decl_ = decl;   // 首版不消费，记录原声明
        }
    }

    // *NAME_MAP 段：每行 *<正整数> <名字>；名字保持原文（含转义不去
    // 转义）；索引重复保留首份并计数；条目残缺（缺索引或缺名字）=
    // 文件级错误（规格 3.7）
    void parse_name_map(const SpToken& kw_tok) {
        for (;;) {
            SpToken t;
            if (!lex_->next(t)) {
                return;
            }
            if (!is_map_ref_form(t)) {
                lex_->push_back(t);   // 段结束（非条目形态）
                return;
            }
            CMString name;
            SpToken name_tok;
            if (!lex_->next(name_tok) || is_keyword_form(name_tok)) {
                spef_error("incomplete *NAME_MAP entry (missing name)",
                           kw_tok.line_, source_);
            }
            name = name_tok.text_;
            // 索引域校验：正整数且在 uint32 登记域内。索引 0 的形态与
            // 引用一致但值非法（正整数不含 0），按「索引解析不出」归入
            // 条目残缺抛文件级错误——与引用处的未登记兜底对称（两处置
            // 的分界：定义处语法错、引用处值域兜底）
            const CMString digits = t.text_.substr(1);
            char* endptr = nullptr;
            const unsigned long long idx =
                std::strtoull(digits.c_str(), &endptr, 10);
            if (endptr != digits.c_str() + digits.size() || idx == 0 ||
                idx > 0xFFFFFFFFULL) {
                spef_error("invalid *NAME_MAP index: " + t.text_, t.line_,
                           source_);
            }
            const auto res = header_.name_map_.emplace(
                static_cast<uint32_t>(idx), std::move(name));
            if (!res.second) {
                ++header_.name_map_dup_count_;   // 保留首份（规格 3.4）
            }
        }
    }

    void require_units_or_throw(size_t line) {
        if (!has_cap_unit_ || !has_res_unit_) {
            spef_error("missing *C_UNIT/*R_UNIT in file header", line,
                       source_);
        }
    }

    SPFileHeader& header_;
    CMString source_;
    SpLexer* lex_ = nullptr;
    CMVector<CMString> discarded_;   // 次要段（PORTS/DEFINE 等）的收集桶
    bool has_cap_unit_ = false;
    bool has_res_unit_ = false;
};

// —— 网段解析（逐块任务 / 整文件解析共用；规格 3.5）——
class NetsParser {
public:
    NetsParser(const SPFileHeader& header, uint32_t value_index,
               bool value_index_given)
        : header_(header),
          value_index_(value_index),
          value_index_given_(value_index_given) {}

    SPChunkResult run(SpLexer& lex) {
        lex_ = &lex;
        SpToken tok;
        // 杂料序列状态：连续杂料 token（之间仅空白/注释相连）记 1 次，
        // 网段关键字结束序列——不随 token 个数膨胀（口径见
        // SPChunkResult::stray_content_count_ 注释）
        bool in_stray_run = false;
        for (;;) {
            if (!lex_->next(tok)) {
                break;
            }
            // 网段识别按 token、与位置无关（行中关键字同样生效——前置
            // 内容只会是上一网的合法记录或网间杂料，都不应让网被无声
            // 丢弃）；quoted 串不是关键字
            if (is_keyword_form(tok) && is_net_keyword_text(tok.text_)) {
                in_stray_run = false;
                if (tok.text_ == "*D_PNET" || tok.text_ == "*R_PNET") {
                    // 物理网整段跳过 + 计数（裁定 12）
                    ++out_.pnet_skip_count_;
                    skip_pnet_section();
                } else {
                    parse_net(tok.text_ == "*R_NET" ? SPNetKind::R_NET
                                                    : SPNetKind::D_NET);
                }
                continue;
            }
            // 网与网之间的杂料：跳过但计数（跳过必须计数——无声丢内容
            // 不可接受）
            if (!in_stray_run) {
                ++out_.stray_content_count_;
                in_stray_run = true;
            }
        }
        return std::move(out_);
    }

private:
    // 单步解析结果：kBroken = 段级破损（整网丢弃——规格 3.7）
    enum class Step { kOk, kBroken };

    // 节点分类结果：ok = false 即未登记索引（记录丢弃，裁定 11）；
    // other_internal = 「别的网的内部节点」（对地/耦合的 external 口径）
    struct NodeClass {
        bool ok = false;
        bool other_internal = false;
    };

    // 每网累加器：破损/跳过网整网丢弃（合并进 out_ 仅在 *END 达成时）
    struct NetAcc {
        CMString name_;
        SPNetKind kind_ = SPNetKind::D_NET;
        double total_ff_ = 0.0;
        double ground_ff_ = 0.0;
        double coupling_ff_ = 0.0;
        uint32_t value_count_ = 1;
        uint64_t cap_count_ = 0;
        uint64_t coupling_cap_count_ = 0;
        uint64_t res_count_ = 0;
        uint64_t external_ground_ = 0;
        uint64_t external_coupling_ = 0;
        uint64_t dropped_ = 0;
        uint64_t map_miss_ = 0;
        uint64_t fallback_ = 0;
        uint64_t conn_ = 0;
        double conn_l_ff_ = 0.0;
        uint64_t unknown_dir_ = 0;
        uint64_t induc_ = 0;
    };

    // 节点切分与分类（规格 3.4）：按最后一个引脚分隔符切分——没有分隔
    // 符 = 端口节点；后半全数字 = 网内内部节点（前半经映射表取原名后
    // 等于当前网名 = 本网，否则别的网）；后半非纯数字 = 实例引脚节点。
    // 实例部分是映射引用时查表，未登记 = 记录丢弃（miss 在此计数）；
    // 引脚名部分不查映射表
    NodeClass resolve_node(const CMString& raw) {
        NodeClass cls;
        const char delim = header_.delimiter_;
        const size_t pos = raw.rfind(delim);
        if (pos == CMString::npos) {
            if (is_map_ref_text(raw)) {
                CMString expanded;
                if (!expand_ref(raw, header_, expanded)) {
                    ++acc_.map_miss_;
                    return cls;   // 未登记端口引用：记录丢弃
                }
            }
            cls.ok = true;   // 端口节点
            return cls;
        }
        CMString inst = raw.substr(0, pos);
        const CMString tail = raw.substr(pos + 1);
        if (is_map_ref_text(inst)) {
            CMString expanded;
            if (!expand_ref(inst, header_, expanded)) {
                ++acc_.map_miss_;
                return cls;   // 未登记实例引用：记录丢弃
            }
            inst = expanded;
        }
        if (is_all_digits(tail)) {
            cls.ok = true;
            cls.other_internal = inst != acc_.name_;
            return cls;
        }
        cls.ok = true;   // 实例引脚节点
        return cls;
    }

    // 裸引用形态（无分隔符但整体是 *<正整数>——端口引用）
    static bool is_map_ref_text(const CMString& s) {
        return s.size() >= 2 && s[0] == '*' && is_all_digits(s.substr(1));
    }

    // 取值规则（裁定 6）：显式下标优先，越界退回缺省规则并计数
    double select_value(const CMVector<double>& vals) {
        uint32_t idx = 0;
        if (value_index_given_) {
            if (value_index_ < vals.size()) {
                idx = value_index_;
            } else {
                idx = sp_default_value_index(
                    static_cast<uint32_t>(vals.size()));
                ++acc_.fallback_;
            }
        } else {
            idx = sp_default_value_index(static_cast<uint32_t>(vals.size()));
        }
        return vals[idx];
    }

    // —— 网段主流程 ——

    void parse_net(SPNetKind kind) {
        SpToken tok;
        // 网段头：<网名> <总电容值>（残缺 = 段级破损——规格 3.7；网名
        // 可为映射引用形态）。头部位读到网段关键字时先放回再破损跳扫
        //——关键字留在流里，紧随的网照常收录（否则被整体跳过、无声
        // 丢网）
        if (!lex_->next(tok)) {
            ++out_.broken_net_count_;
            return;   // 流尽：无可放回，skip 对 EOF 立即返回
        }
        if (is_keyword_tok(tok)) {
            lex_->push_back(tok);
            ++out_.broken_net_count_;
            skip_to_next_net();
            return;
        }
        CMString name;
        if (is_map_ref_form(tok)) {
            if (!expand_ref(tok.text_, header_, name)) {
                // 未登记网名：整网跳过（裁定 11），不属破损
                ++out_.name_map_miss_count_;
                skip_to_next_net();
                return;
            }
        } else {
            name = tok.text_;
        }
        if (!lex_->next(tok)) {
            ++out_.broken_net_count_;
            return;
        }
        if (is_keyword_tok(tok)) {
            lex_->push_back(tok);
            ++out_.broken_net_count_;
            skip_to_next_net();
            return;
        }
        CMVector<double> vals;
        if (!sp_parse_value(tok.text_, vals)) {
            ++out_.broken_net_count_;
            skip_to_next_net();
            return;
        }
        acc_ = NetAcc{};
        acc_.name_ = name;
        acc_.kind_ = kind;
        acc_.value_count_ = static_cast<uint32_t>(vals.size());
        // 行首总电容进入合计：符号校验只看选中参与合计的那个值（多值
        // 形态下未选中的值不触发校验——裁定），判**文件数值**（乘换算
        // 系数之前——第 H 节第 1 条：负的次正规数 × 小于 1 的系数会下
        // 溢成 -0.0，判换算后值漏判），为负 = 段级破损
        const double selected = select_value(vals);
        if (selected < 0.0) {
            ++out_.broken_net_count_;
            skip_to_next_net();
            return;
        }
        acc_.total_ff_ = scaled_nonneg_cap(selected, header_.cap_scale_ff_);
        // 可选行尾 *V <置信度>（语法消费；残缺 = 段级破损）
        SpToken nxt;
        if (!lex_->next(nxt)) {
            ++out_.broken_net_count_;   // 到文件尾无 *END
            return;
        }
        if (is_keyword_form(nxt) && nxt.text_ == "*V") {
            if (!next_number(*lex_, nxt)) {   // 置信度数值残缺 = 段级破损
                ++out_.broken_net_count_;
                skip_to_next_net();
                return;
            }
        } else {
            lex_->push_back(nxt);
        }
        if (parse_body(kind) == Step::kBroken) {
            ++out_.broken_net_count_;
            skip_to_next_net();
            return;
        }
        merge_acc();
    }

    // 网段体：子段关键字任意顺序、全部可缺省、关键字后可零条记录
    //（规格 3.5）；*END = 段完成；到文件尾或下一个网段关键字（位置无
    // 关——网内关键字说明本网缺 *END）都没遇到 *END = 段级破损
    Step parse_body(SPNetKind kind) {
        for (;;) {
            SpToken tok;
            if (!lex_->next(tok)) {
                return Step::kBroken;   // *END 缺失（到文件尾）
            }
            if (!is_keyword_tok(tok)) {
                return Step::kBroken;   // 网层裸 token / 杂散引用 = 段级破损
            }
            if (is_keyword_form(tok) && is_net_keyword_text(tok.text_)) {
                lex_->push_back(tok);
                return Step::kBroken;   // *END 缺失（到下一个网段关键字）
            }
            if (tok.text_ == "*END") {
                return Step::kOk;
            }
            Step s = Step::kOk;
            if (kind == SPNetKind::D_NET) {
                if (tok.text_ == "*CONN") {
                    s = parse_conn_section();
                } else if (tok.text_ == "*CAP") {
                    s = parse_cap_section();
                } else if (tok.text_ == "*RES") {
                    s = parse_pair_records(true);
                } else if (tok.text_ == "*INDUC") {
                    s = parse_pair_records(false);
                } else {
                    return Step::kBroken;   // 不认识的 * 关键字
                }
            } else {
                // 缩减网带可选的连接段（标准允许，商业工具文件里可能出
                // 现）：*CONN 按分布式网同一口径只统计不取值；其余按
                // 缩减模型语法消费（裁定 5）
                s = tok.text_ == "*CONN" ? parse_conn_section()
                                         : parse_rnet_construct(tok);
            }
            if (s != Step::kOk) {
                return s;
            }
        }
    }

    // R_NET 段内缩减模型构造（*CONN 不在此处理——parse_body 分派给通
    // 用连接段）：*DRV <驱动引脚> *CELL <单元名> *C2_R1_C1 <c2> <r1>
    // <c1>（π 模型——c2/c1 是电容、r1 是电阻）+ *LOADS 下逐负载 *RC
    // <引脚> <埃尔莫尔延迟（单值）> + *Q <极点数> / *K <留数...>。
    // 全部为不消费的数值字段（裁定）：只要求合法数值（sp_parse_number
    // 把关有限性），负值接受、不取值——不消费的值不能让已收录的行首
    // 总电容整网丢掉（上游读取器对埃尔莫尔延迟同样不查符号）。其它关
    // 键字 = 不认识 → 破损（含 *CAP/*RES——缩减网没有 RC 明细段）
    Step parse_rnet_construct(const SpToken& kw) {
        SpToken t;
        if (kw.text_ == "*LOADS") {
            return Step::kOk;
        }
        if (kw.text_ == "*DRV" || kw.text_ == "*CELL") {
            return next_plain_token(*lex_, t) ? Step::kOk : Step::kBroken;
        }
        if (kw.text_ == "*C2_R1_C1") {
            // π 模型三值（c2 电容 / r1 电阻 / c1 电容）：不消费，负值接受
            for (int i = 0; i < 3; ++i) {
                if (!next_number(*lex_, t)) {
                    return Step::kBroken;
                }
            }
            return Step::kOk;
        }
        if (kw.text_ == "*RC") {
            if (!next_plain_token(*lex_, t)) {
                return Step::kBroken;   // 负载引脚
            }
            // 埃尔莫尔延迟（单值）：不消费，负值接受
            return next_number(*lex_, t) ? Step::kOk : Step::kBroken;
        }
        if (kw.text_ == "*Q") {
            return next_number(*lex_, t) ? Step::kOk : Step::kBroken;
        }
        if (kw.text_ == "*K") {
            if (!next_number(*lex_, t)) {
                return Step::kBroken;
            }
            // 留数可多个：数值串到关键字为止（停止 token 回退不消费）
            for (;;) {
                if (!lex_->next(t)) {
                    break;   // EOF 交回网段体（判 *END 缺失）
                }
                if (is_keyword_tok(t)) {
                    lex_->push_back(t);
                    break;
                }
                double dummy = 0.0;
                if (!sp_parse_number(t.text_, dummy)) {
                    return Step::kBroken;   // 数值串中的杂料 = 段级破损
                }
            }
            return Step::kOk;
        }
        return Step::kBroken;   // D_NET 族关键字 / 未知关键字
    }

    // —— *CONN 段（内容不入库，裁定 3：只统计 *I/*P 行数与 *L 观测和）——

    Step parse_conn_section() {
        for (;;) {
            SpToken tok;
            if (!lex_->next(tok)) {
                return Step::kOk;   // 流尽交回网段体（判 *END 缺失）
            }
            if (!is_keyword_tok(tok)) {
                return Step::kBroken;
            }
            if (tok.text_ == "*I" || tok.text_ == "*P") {
                const Step s = parse_conn_record();
                if (s != Step::kOk) {
                    return s;
                }
                continue;
            }
            if (tok.text_ == "*N") {
                const Step s = parse_coord_record();
                if (s != Step::kOk) {
                    return s;
                }
                continue;
            }
            lex_->push_back(tok);   // 其它关键字 → 段结束
            return Step::kOk;
        }
    }

    // *I / *P <名字> [方向] [*L 值] [*C x y] [*S 上升 下降] [*D 单元名]；
    // 方向 I/O/B 之外计未知不报错（规格 3.5）
    Step parse_conn_record() {
        SpToken tok;
        if (!next_plain_token(*lex_, tok)) {
            return Step::kBroken;   // 缺名字
        }
        const NodeClass node = resolve_node(tok.text_);
        const bool kept = node.ok;
        // 方向（可选；具名节点的 *I 行可无方向——§2.5 方言事实）
        SpToken dir;
        if (lex_->next(dir)) {
            if (is_keyword_tok(dir)) {
                lex_->push_back(dir);
            } else if (dir.text_ != "I" && dir.text_ != "O" &&
                       dir.text_ != "B") {
                if (kept) {
                    ++acc_.unknown_dir_;
                }
            }
        }
        // 属性循环
        for (;;) {
            SpToken attr;
            if (!lex_->next(attr)) {
                break;   // 流尽交回上层
            }
            if (!is_keyword_tok(attr)) {
                return Step::kBroken;   // 属性后杂散 token
            }
            if (attr.text_ == "*L") {
                // 引脚负载电容（观测值，不入库）：不消费字段——负值接
                // 受、照加观测和（裁定：不消费的值不能让已收录合计丢掉）
                CMVector<double> vals;
                if (!next_value(*lex_, attr, vals)) {
                    return Step::kBroken;
                }
                if (kept) {
                    acc_.conn_l_ff_ +=
                        select_value(vals) * header_.cap_scale_ff_;
                }
                continue;
            }
            if (attr.text_ == "*C") {
                if (!next_number(*lex_, attr) || !next_number(*lex_, attr)) {
                    return Step::kBroken;
                }
                continue;
            }
            if (attr.text_ == "*S") {
                // 转换时间（上升/下降）：不消费字段，数值且有限即可，
                // 负值接受（裁定）
                CMVector<double> first;
                if (!next_value(*lex_, attr, first)) {
                    return Step::kBroken;
                }
                CMVector<double> second;
                if (!next_value(*lex_, attr, second)) {
                    return Step::kBroken;
                }
                continue;
            }
            if (attr.text_ == "*D") {
                if (!next_plain_token(*lex_, attr)) {
                    return Step::kBroken;
                }
                continue;
            }
            lex_->push_back(attr);   // 其它关键字 → 记录结束
            break;
        }
        if (kept) {
            ++acc_.conn_;
        } else {
            ++acc_.dropped_;   // 未登记引用：记录丢弃（不计任何合计）
        }
        return Step::kOk;
    }

    // *N <节点> *C x y（坐标记录——接受并忽略，裁定 12）
    Step parse_coord_record() {
        SpToken tok;
        if (!next_plain_token(*lex_, tok)) {
            return Step::kBroken;
        }
        const NodeClass node = resolve_node(tok.text_);
        SpToken star_c;
        if (!lex_->next(star_c)) {
            return Step::kBroken;
        }
        if (star_c.text_ != "*C") {
            // *C 期待位读到其它 token：真关键字（如 *D_NET）放回——破
            // 损跳扫必须能命中它，否则紧随的网被无声跳过；裸 token 归
            // 入记录残缺（消费即可）
            if (is_keyword_tok(star_c)) {
                lex_->push_back(star_c);
            }
            return Step::kBroken;
        }
        if (!next_number(*lex_, star_c) || !next_number(*lex_, star_c)) {
            return Step::kBroken;
        }
        if (!node.ok) {
            ++acc_.dropped_;
        }
        return Step::kOk;
    }

    // —— *CAP 段：[编号] <节点1> [节点2] <值>（编号可选非负整数——
    //    OpenROAD 写出器可省略，解析后不参与逻辑，§2.5）——
    Step parse_cap_section() {
        for (;;) {
            SpToken tok;
            if (!lex_->next(tok)) {
                return Step::kOk;
            }
            if (is_keyword_tok(tok)) {
                lex_->push_back(tok);
                return Step::kOk;
            }
            SpToken n1 = tok;
            if (is_all_digits(tok.text_)) {
                if (!next_plain_token(*lex_, n1)) {
                    return Step::kBroken;
                }
            }
            SpToken second;
            if (!next_plain_token(*lex_, second)) {
                return Step::kBroken;   // 缺值（或值位是关键字）
            }
            CMVector<double> vals;
            if (sp_parse_value(second.text_, vals)) {
                // 对地电容（进入合计：符号校验只看选中的值，判**文件数
                // 值**——第 H 节第 1 条同口径，负 = 段级破损——负值不能
                // 进入合计）
                const double selected = select_value(vals);
                if (selected < 0.0) {
                    return Step::kBroken;
                }
                const double ff =
                    scaled_nonneg_cap(selected, header_.cap_scale_ff_);
                const NodeClass c1 = resolve_node(n1.text_);
                if (!c1.ok) {
                    ++acc_.dropped_;
                    continue;
                }
                ++acc_.cap_count_;
                if (c1.other_internal) {
                    ++acc_.external_ground_;
                } else {
                    acc_.ground_ff_ += ff;
                }
                continue;
            }
            // 耦合电容（两个节点）——值读入独立 token，second 保持节点 2；
            // 进入合计：选中值为负（判文件数值——第 H 节第 1 条同口径）
            // = 段级破损
            CMVector<double> cap_vals;
            SpToken val_tok;
            if (!next_value(*lex_, val_tok, cap_vals)) {
                return Step::kBroken;
            }
            const double cap_selected = select_value(cap_vals);
            if (cap_selected < 0.0) {
                return Step::kBroken;
            }
            const double ff =
                scaled_nonneg_cap(cap_selected, header_.cap_scale_ff_);
            const NodeClass c1 = resolve_node(n1.text_);
            const NodeClass c2 = resolve_node(second.text_);
            if (!c1.ok || !c2.ok) {
                ++acc_.dropped_;
                continue;
            }
            ++acc_.cap_count_;
            if (c1.other_internal && c2.other_internal) {
                ++acc_.external_coupling_;   // 两端都是别的网内部 → 不计
            } else {
                acc_.coupling_ff_ += ff;     // 一端属本网即整条计入
                ++acc_.coupling_cap_count_;
            }
        }
    }

    // *RES / *INDUC 段：[编号] <节点1> <节点2> <值>——首版只计数，值
    // 不保留（裁定 2；INDUC 记录忽略并计数，裁定 12）：不消费的数值字
    // 段只要求合法数值，负值接受（裁定——不消费的值不能让已收录合计
    // 丢掉）
    Step parse_pair_records(bool is_res) {
        for (;;) {
            SpToken tok;
            if (!lex_->next(tok)) {
                return Step::kOk;
            }
            if (is_keyword_tok(tok)) {
                lex_->push_back(tok);
                return Step::kOk;
            }
            SpToken n1 = tok;
            if (is_all_digits(tok.text_)) {
                if (!next_plain_token(*lex_, n1)) {
                    return Step::kBroken;
                }
            }
            SpToken n2;
            if (!next_plain_token(*lex_, n2)) {
                return Step::kBroken;
            }
            CMVector<double> vals;
            if (sp_parse_value(n2.text_, vals)) {
                return Step::kBroken;   // 电阻/电感必两节点——单节点形态残缺
            }
            SpToken val_tok;   // 值读入独立 token——n2 保持节点 2 原文
            if (!next_value(*lex_, val_tok, vals)) {
                return Step::kBroken;
            }
            const NodeClass c1 = resolve_node(n1.text_);
            const NodeClass c2 = resolve_node(n2.text_);
            if (!c1.ok || !c2.ok) {
                ++acc_.dropped_;
                continue;
            }
            if (is_res) {
                ++acc_.res_count_;
            } else {
                ++acc_.induc_;
            }
        }
    }

    // 读下一 token 并按多值形态解析为数值（真关键字形态或非法 = false）。
    // 读到真关键字先放回流（理由同 next_number——破损跳扫必须能命中
    // 它，否则紧随的网段被无声跳过）
    static bool next_value(SpLexer& lex, SpToken& tok,
                           CMVector<double>& vals) {
        if (!lex.next(tok)) {
            return false;
        }
        if (is_keyword_tok(tok)) {
            lex.push_back(tok);
            return false;
        }
        return sp_parse_value(tok.text_, vals);
    }

    // —— 跳扫（破损网 / 名字未登记网 / 物理网段）——

    // 到下一个网段关键字（位置无关，回退不消费）或文件尾
    void skip_to_next_net() {
        for (;;) {
            SpToken tok;
            if (!lex_->next(tok)) {
                return;
            }
            if (is_keyword_form(tok) && is_net_keyword_text(tok.text_)) {
                lex_->push_back(tok);
                return;
            }
        }
    }

    // 物理网段：到 *END（消费）或下一个网段关键字（位置无关）或文件尾
    void skip_pnet_section() {
        for (;;) {
            SpToken tok;
            if (!lex_->next(tok)) {
                return;
            }
            if (is_keyword_form(tok)) {
                if (tok.text_ == "*END") {
                    return;
                }
                if (is_net_keyword_text(tok.text_)) {
                    lex_->push_back(tok);
                    return;
                }
            }
        }
    }

    // *END 达成：累加器并入产物（规格 3.3/3.5 的全部观测口径）
    void merge_acc() {
        SPNet net;
        net.name_ = acc_.name_;
        net.kind_ = acc_.kind_;
        net.total_cap_reported_ff_ = acc_.total_ff_;
        net.ground_cap_ff_ = acc_.ground_ff_;
        net.coupling_cap_ff_ = acc_.coupling_ff_;
        net.value_count_ = acc_.value_count_;
        net.cap_count_ = acc_.cap_count_;
        net.coupling_cap_count_ = acc_.coupling_cap_count_;
        net.res_count_ = acc_.res_count_;
        out_.nets_.push_back(std::move(net));
        ++out_.net_count_;
        if (acc_.kind_ == SPNetKind::D_NET) {
            ++out_.dnet_count_;
        } else {
            ++out_.rnet_count_;
        }
        if (acc_.value_count_ > 1) {
            ++out_.multi_value_net_count_;
        }
        out_.value_count_max_ =
            acc_.value_count_ > out_.value_count_max_ ? acc_.value_count_
                                                      : out_.value_count_max_;
        out_.name_map_miss_count_ += acc_.map_miss_;
        out_.dropped_record_count_ += acc_.dropped_;
        out_.value_index_fallback_count_ += acc_.fallback_;
        out_.cap_count_ += acc_.cap_count_;
        out_.coupling_cap_count_ += acc_.coupling_cap_count_;
        out_.res_count_ += acc_.res_count_;
        out_.external_ground_cap_count_ += acc_.external_ground_;
        out_.external_coupling_cap_count_ += acc_.external_coupling_;
        out_.conn_count_ += acc_.conn_;
        out_.conn_pin_cap_obs_sum_ff_ += acc_.conn_l_ff_;
        out_.unknown_direction_count_ += acc_.unknown_dir_;
        out_.induc_count_ += acc_.induc_;
    }

    const SPFileHeader& header_;
    uint32_t value_index_;
    bool value_index_given_;
    SpLexer* lex_ = nullptr;
    SPChunkResult out_;
    NetAcc acc_;
};

CMString read_file_or_throw(const CMString& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("SPEF: file not readable: " + path);
    }
    return CMString((std::istreambuf_iterator<char>(in)),
                    std::istreambuf_iterator<char>());
}

// gzip 魔数嗅探（0x1f 0x8b）——流程层入口已解压（同 waveform 裁定
// IV-1），直连 C++ API 不支持压缩输入，明确报错指引解压
void reject_gzip(const CMString& text, const CMString& source) {
    if (text.size() >= 2 && static_cast<unsigned char>(text[0]) == 0x1f &&
        static_cast<unsigned char>(text[1]) == 0x8b) {
        throw std::runtime_error(
            "SPEF: gzip compressed input not supported by direct C++ API "
            "(flow layer decompresses first), decompress first: " + source);
    }
}

}  // namespace

SPFileHeader sp_parse_header_text(const CMString& text,
                                  const CMString& source_name) {
    SpLexer lex;
    lex.reset(text.data(), text.size());
    SPFileHeader header;
    header.source_file_ = source_name;
    HeaderParser parser(header, source_name);
    parser.parse(lex);
    return header;
}

SPFileHeader sp_parse_header_file(const CMString& path) {
    const CMString text = read_file_or_throw(path);
    reject_gzip(text, path);
    return sp_parse_header_text(text, path);
}

SPFileChunkPlan sp_plan_file_chunks(const CMString& path, uint64_t chunk_size,
                                    SPFileHeader& header_out) {
    const CMString text = read_file_or_throw(path);
    reject_gzip(text, path);
    SpLexer lex;
    lex.reset(text.data(), text.size());
    // 头部解析含全部文件级校验（规格 3.8：切块扫描照 3.7 抛文件级错误）；
    // 头部产物随计划一并产出（header_out），流程层免二次整读
    HeaderParser parser(header_out, path);
    const uint64_t first_net = parser.parse(lex);
    header_out.source_file_ = path;
    SPFileChunkPlan plan;
    plan.file_name_ = path;
    plan.file_size_ = text.size();
    if (first_net == kNoOffset) {
        plan.header_end_ = text.size();   // 纯头部文件：零块（空结果放行）
        return plan;
    }
    plan.header_end_ = first_net;
    // 按网段边界切块：累计字节超过 chunk_size 后在下一个网段关键字处
    // 收口；单个网段超过 chunk_size 不拆分（整段进一块——只认网段关键
    // 字边界，规格 3.8）。「行首」判定只服务此处（块边界仍要求行首）：
    // 行中的网段关键字不切出新块，所在行留在原块——块解析按 token 识
    // 别网段（位置无关），照常收录该网；块文本不以网段关键字开头（理
    // 论形态，如上游手工切块）也安全——run 对网段区杂料是跳过 + 计数
    //（stray_content_count_），不会丢网
    uint64_t chunk_start = first_net;
    SpToken tok;
    while (lex.next(tok)) {
        if (tok.line_start_ && !tok.quoted_ &&
            is_net_keyword_text(tok.text_) &&
            tok.start_ - chunk_start > chunk_size) {
            SPFileChunk chunk;
            chunk.start_ = chunk_start;
            chunk.end_ = tok.start_;
            plan.chunks_.push_back(std::move(chunk));
            chunk_start = tok.start_;
        }
    }
    SPFileChunk last;
    last.start_ = chunk_start;
    last.end_ = text.size();
    plan.chunks_.push_back(std::move(last));
    return plan;
}

SPChunkResult sp_parse_chunk_text(const CMString& text,
                                  const SPFileHeader& header,
                                  uint32_t value_index, bool value_index_given,
                                  const CMString& source_name) {
    (void)source_name;   // 兼容层不存储（段级兜底不抛错，无报错定位需求）
    SpLexer lex;
    lex.reset(text.data(), text.size());
    NetsParser parser(header, value_index, value_index_given);
    return parser.run(lex);
}

SPChunkResult sp_parse_spef_text(const CMString& text,
                                 SPFileHeader& header_out, uint32_t value_index,
                                 bool value_index_given,
                                 const CMString& source_name) {
    SpLexer lex;
    lex.reset(text.data(), text.size());
    HeaderParser header_parser(header_out, source_name);
    header_parser.parse(lex);
    header_out.source_file_ = source_name;
    NetsParser parser(header_out, value_index, value_index_given);
    return parser.run(lex);
}

SPChunkResult sp_parse_spef_file(const CMString& path,
                                 SPFileHeader& header_out, uint32_t value_index,
                                 bool value_index_given) {
    const CMString text = read_file_or_throw(path);
    reject_gzip(text, path);
    return sp_parse_spef_text(text, header_out, value_index, value_index_given,
                              path);
}

SPChunkResult sp_parse_spef_file(const CMString& path) {
    SPFileHeader header;
    return sp_parse_spef_file(path, header, 0, false);
}

CMString sp_read_byte_range(const CMString& path, uint64_t start,
                            uint64_t end) {
    if (start > end) {
        throw std::runtime_error(
            "SPEF: invalid byte range [" + std::to_string(start) + ", " +
            std::to_string(end) + "): start > end: " + path);
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("SPEF: file not readable: " + path);
    }
    in.seekg(0, std::ios::end);
    const auto size = in.tellg();
    if (size < 0 || end > static_cast<uint64_t>(size)) {
        throw std::runtime_error(
            "SPEF: byte range [" + std::to_string(start) + ", " +
            std::to_string(end) + ") exceeds file size " +
            std::to_string(size) + ": " + path);
    }
    in.seekg(static_cast<std::streamoff>(start));
    CMString out;
    out.resize(static_cast<size_t>(end - start));
    if (!out.empty()) {
        in.read(out.data(), static_cast<std::streamsize>(out.size()));
        if (in.gcount() != static_cast<std::streamsize>(out.size())) {
            throw std::runtime_error("SPEF: short read on byte range: " +
                                     path);
        }
    }
    return out;
}

}  // namespace fly
