#include <emir/timing/cpp/tm_parser.h>

#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>

namespace fly {
namespace {

// —— 词法层：token = 括号 / 带引号字符串（去反斜杠转义）/ 裸原子 ——
class TMTokenReader {
public:
    enum class Kind { kLParen, kRParen, kString, kAtom, kEnd };
    struct Token {
        Kind kind = Kind::kEnd;
        CMString text;
        size_t line = 0;
    };

    TMTokenReader() = default;
    TMTokenReader(const char* data, size_t size) : data_(data), size_(size) {}

    void reset() {
        pos_ = 0;
        line_ = 1;
        has_peek_ = false;
    }

    const Token& peek() {
        if (!has_peek_) {
            peek_ = next_impl();
            has_peek_ = true;
        }
        return peek_;
    }

    Token next() {
        if (has_peek_) {
            Token t = std::move(peek_);
            has_peek_ = false;
            return t;
        }
        return next_impl();
    }

private:
    static bool is_space(char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' ||
               c == '\v';
    }

    Token next_impl() {
        while (pos_ < size_ && is_space(data_[pos_])) {
            if (data_[pos_] == '\n') {
                ++line_;
            }
            ++pos_;
        }
        Token t;
        t.line = line_;
        if (pos_ >= size_) {
            return t;
        }
        const char c = data_[pos_];
        if (c == '(') {
            ++pos_;
            t.kind = Kind::kLParen;
            return t;
        }
        if (c == ')') {
            ++pos_;
            t.kind = Kind::kRParen;
            return t;
        }
        if (c == '"') {
            ++pos_;
            while (pos_ < size_ && data_[pos_] != '"') {
                if (data_[pos_] != '\\') {
                    t.text.push_back(data_[pos_]);
                }
                ++pos_;
            }
            if (pos_ >= size_) {
                throw std::runtime_error("TWF: unterminated quoted string "
                                         "at line " +
                                         std::to_string(line_));
            }
            ++pos_;
            t.kind = Kind::kString;
            return t;
        }
        while (pos_ < size_ && !is_space(data_[pos_]) && data_[pos_] != '(' &&
               data_[pos_] != ')' && data_[pos_] != '"') {
            t.text.push_back(data_[pos_]);
            ++pos_;
        }
        t.kind = Kind::kAtom;
        return t;
    }

    const char* data_ = nullptr;
    size_t size_ = 0;
    size_t pos_ = 0;
    size_t line_ = 1;
    Token peek_;
    bool has_peek_ = false;
};

[[noreturn]] void stream_error(const CMString& what, size_t line) {
    throw std::runtime_error("TWF: " + what + " at line " +
                             std::to_string(line));
}

// 数值字段解析：「*」→ valid=false；「a:b」对；单标量 a（min=max）。
// 形态非法返回 false（调用方按条目级破损跳过计数）。
bool parse_field(const CMString& text, TMRange& out, bool& valid) {
    valid = false;
    if (text == "*") {
        return true;
    }
    const auto parse_num = [&text](size_t begin, size_t end, double& value) {
        if (begin >= end) {
            return false;
        }
        const CMString sub = text.substr(begin, end - begin);
        char* endptr = nullptr;
        value = std::strtod(sub.c_str(), &endptr);
        return endptr != sub.c_str() && *endptr == '\0';
    };
    const size_t colon = text.find(':');
    if (colon == CMString::npos) {
        double v = 0.0;
        if (!parse_num(0, text.size(), v)) {
            return false;
        }
        out.min_ = out.max_ = v;
        valid = true;
        return true;
    }
    double lo = 0.0;
    double hi = 0.0;
    if (!parse_num(0, colon, lo) || !parse_num(colon + 1, text.size(), hi)) {
        return false;
    }
    out.min_ = lo;
    out.max_ = hi;
    valid = true;
    return true;
}

bool parse_number(const CMString& text, double& value) {
    char* endptr = nullptr;
    value = std::strtod(text.c_str(), &endptr);
    return endptr != text.c_str() && *endptr == '\0';
}

// —— 语法层：两遍扫描（第一遍 HEADER + WAVEFORM 建时钟表，第二遍
// CAUSED_BY 条目——时钟名先于条目全部登记，合并时钟 id 即刻可判）——
class TWFParser {
public:
    TMTimingFile parse(const CMString& text, const CMString& source_name) {
        out_.source_file_ = source_name;
        reader_ = TMTokenReader(text.data(), text.size());
        expect_top();
        run_constructs(/*header_pass=*/true);
        reader_.reset();
        expect_top();
        run_constructs(/*header_pass=*/false);
        if (reader_.peek().kind != TMTokenReader::Kind::kEnd) {
            stream_error("trailing content after top construct",
                         reader_.peek().line);
        }
        return std::move(out_);
    }

private:
    using Kind = TMTokenReader::Kind;
    using Token = TMTokenReader::Token;

    void expect_top() {
        Token t = reader_.next();
        if (t.kind != Kind::kLParen) {
            stream_error("expected '(TIMING_WINDOWS'", t.line);
        }
        t = reader_.next();
        if (t.kind != Kind::kAtom || t.text != "TIMING_WINDOWS") {
            stream_error("expected '(TIMING_WINDOWS'", t.line);
        }
    }

    void run_constructs(bool header_pass) {
        for (;;) {
            Token t = reader_.peek();
            if (t.kind == Kind::kEnd) {
                stream_error("unexpected end of stream (unbalanced top)",
                             t.line);
            }
            if (t.kind == Kind::kRParen) {
                reader_.next();
                return;
            }
            if (t.kind != Kind::kLParen) {
                stream_error("stray token in top construct", t.line);
            }
            reader_.next();
            const Token kw = reader_.next();
            if (kw.kind != Kind::kAtom) {
                skip_construct(1);
                ++out_.unknown_construct_count_;
                continue;
            }
            if (header_pass) {
                if (kw.text == "HEADER") {
                    parse_header();
                } else if (kw.text == "WAVEFORM") {
                    parse_waveform();
                } else {
                    skip_construct(1);
                }
            } else {
                if (kw.text == "CAUSED_BY") {
                    parse_caused_by();
                } else {
                    skip_construct(1);
                }
            }
        }
    }

    // 平衡跳过当前未闭合构造（depth = 已消费且未闭合的 '(' 数）
    void skip_construct(int depth) {
        while (depth > 0) {
            const Token t = reader_.next();
            if (t.kind == Kind::kEnd) {
                stream_error("unexpected end of stream (unbalanced construct)",
                             t.line);
            }
            if (t.kind == Kind::kLParen) {
                ++depth;
            } else if (t.kind == Kind::kRParen) {
                --depth;
            }
        }
    }

    // 消费至当前子构造闭合 ')'（嵌套 '(' 平衡跳过）
    void close_flat() {
        int depth = 1;
        while (depth > 0) {
            const Token t = reader_.next();
            if (t.kind == Kind::kEnd) {
                stream_error("unexpected end of stream (unbalanced construct)",
                             t.line);
            }
            if (t.kind == Kind::kLParen) {
                ++depth;
            } else if (t.kind == Kind::kRParen) {
                --depth;
            }
        }
    }

    void parse_header() {
        for (;;) {
            Token t = reader_.peek();
            if (t.kind == Kind::kRParen) {
                reader_.next();
                return;
            }
            if (t.kind == Kind::kEnd) {
                stream_error("unexpected end of stream in HEADER", t.line);
            }
            if (t.kind != Kind::kLParen) {
                reader_.next();
                continue;
            }
            reader_.next();
            const Token kw = reader_.next();
            if (kw.kind != Kind::kAtom) {
                skip_construct(1);
                continue;
            }
            if (kw.text == "VERSION" || kw.text == "DESIGN") {
                const Token v = reader_.next();
                if (v.kind == Kind::kString) {
                    (kw.text == "VERSION" ? out_.version_ : out_.design_) =
                        v.text;
                }
            } else if (kw.text == "TIME_SCALE") {
                const Token v = reader_.next();
                double s = 0.0;
                if (v.kind == Kind::kAtom && parse_number(v.text, s)) {
                    out_.time_scale_sec_ = s;
                }
            } else if (kw.text == "VOLTAGE_THRESHOLD") {
                double lo = 0.0;
                double hi = 0.0;
                const Token a = reader_.next();
                const Token b = reader_.next();
                if (a.kind == Kind::kAtom && b.kind == Kind::kAtom &&
                    parse_number(a.text, lo) && parse_number(b.text, hi)) {
                    out_.vth_low_ = lo;
                    out_.vth_high_ = hi;
                }
            } else if (kw.text == "DEFAULT_INPUT_SLEW") {
                while (reader_.peek().kind == Kind::kAtom) {
                    double v = 0.0;
                    const Token a = reader_.next();
                    if (parse_number(a.text, v)) {
                        out_.default_input_slew_.push_back(v);
                    }
                }
            }
            // 其余头部构造静默忽略（DATE/PROGRAM/PVT 等，无消费者）
            close_flat();
        }
    }

    void parse_waveform() {
        TMClock clock;
        Token t = reader_.next();
        if (t.kind != Kind::kString) {
            skip_construct(1);
            ++out_.unknown_construct_count_;
            return;
        }
        clock.name_ = t.text;
        t = reader_.next();
        if (t.kind != Kind::kAtom || !parse_number(t.text, clock.period_)) {
            skip_construct(1);
            ++out_.unknown_construct_count_;
            return;
        }
        for (;;) {
            const Token p = reader_.peek();
            if (p.kind == Kind::kRParen) {
                reader_.next();
                break;
            }
            if (p.kind == Kind::kEnd) {
                stream_error("unexpected end of stream in WAVEFORM", p.line);
            }
            if (p.kind != Kind::kLParen) {
                reader_.next();
                continue;
            }
            reader_.next();
            const Token kw = reader_.next();
            const Token val = reader_.next();
            if (kw.kind == Kind::kAtom && val.kind == Kind::kAtom) {
                double v = 0.0;
                if (parse_number(val.text, v)) {
                    if (kw.text == "POSEDGE") {
                        clock.posedge_ = v;
                    } else if (kw.text == "NEGEDGE") {
                        clock.negedge_ = v;
                    }
                }
            }
            close_flat();
        }
        if (out_.clock_index_.count(clock.name_) != 0) {
            ++out_.unknown_construct_count_;
            return;
        }
        const double k = out_.time_scale_sec_ * 1e9;
        clock.period_ *= k;
        clock.posedge_ *= k;
        clock.negedge_ *= k;
        out_.clock_index_.emplace(clock.name_,
                                  static_cast<uint32_t>(out_.clocks_.size()));
        out_.clocks_.push_back(std::move(clock));
    }

    void parse_caused_by() {
        uint32_t clock_id = kTMNoClock;
        Token t = reader_.next();
        if (t.kind == Kind::kString) {
            const auto it = out_.clock_index_.find(t.text);
            if (it != out_.clock_index_.end()) {
                clock_id = it->second;
            } else {
                ++out_.missing_clock_count_;
            }
        } else if (!(t.kind == Kind::kAtom && t.text == "NULL")) {
            skip_construct(1);
            ++out_.unknown_construct_count_;
            return;
        }
        // 可选边沿原子（RISE/FALL，语义不入库）
        if (reader_.peek().kind == Kind::kAtom) {
            reader_.next();
        }
        for (;;) {
            const Token p = reader_.peek();
            if (p.kind == Kind::kRParen) {
                reader_.next();
                return;
            }
            if (p.kind == Kind::kEnd) {
                stream_error("unexpected end of stream in CAUSED_BY",
                             p.line);
            }
            if (p.kind != Kind::kLParen) {
                reader_.next();
                continue;
            }
            reader_.next();
            const Token kw = reader_.next();
            if (kw.kind != Kind::kAtom) {
                skip_construct(1);
                ++out_.unknown_construct_count_;
                continue;
            }
            if (kw.text == "NET") {
                parse_record(false, clock_id);
            } else if (kw.text == "PIN") {
                parse_record(true, clock_id);
            } else if (kw.text == "CONSTANT") {
                const Token n = reader_.next();
                if (n.kind == Kind::kString) {
                    TMNameTiming e;
                    e.name_ = n.text;
                    e.clock_id_ = clock_id;
                    e.set_constant();
                    out_.upsert_timing(std::move(e));
                } else {
                    ++out_.bad_record_count_;
                }
                close_flat();
            } else {
                skip_construct(1);
                ++out_.unknown_construct_count_;
            }
        }
    }

    // NET/PIN 记录：CONSTANT 前置形态 / 名字 + 八对字段 + 可选 C|D 标记
    void parse_record(bool pin_kind, uint32_t clock_id) {
        TMNameTiming e;
        e.clock_id_ = clock_id;
        if (pin_kind) {
            e.set_pin_kind();
        }
        Token t = reader_.next();
        if (t.kind == Kind::kAtom && t.text == "CONSTANT") {
            const Token n = reader_.next();
            if (n.kind != Kind::kString) {
                recover_record();
                ++out_.bad_record_count_;
                return;
            }
            e.name_ = n.text;
            e.set_constant();
            close_flat();
            out_.upsert_timing(std::move(e));
            return;
        }
        if (t.kind != Kind::kString) {
            recover_record();
            ++out_.bad_record_count_;
            return;
        }
        e.name_ = t.text;

        CMVector<Token> fields;
        bool wellformed = true;
        for (;;) {
            const Token f = reader_.next();
            if (f.kind == Kind::kRParen) {
                break;
            }
            if (f.kind == Kind::kEnd) {
                stream_error("unexpected end of stream in record", f.line);
            }
            if (f.kind != Kind::kAtom) {
                wellformed = false;
                recover_record();
                break;
            }
            fields.push_back(std::move(f));
        }
        if (!wellformed || fields.size() < 8 || fields.size() > 9) {
            ++out_.bad_record_count_;
            return;
        }
        if (fields.size() == 9) {
            if (fields[8].text == "C") {
                ++out_.cd_flag_c_count_;
            } else if (fields[8].text == "D") {
                ++out_.cd_flag_d_count_;
            } else {
                ++out_.bad_record_count_;
                return;
            }
        }

        // 八对字段：RTW Rt RDr RSlk FTW Ft FDr FSlk（文件序）
        const double k = out_.time_scale_sec_ * 1e9;
        TMRange values[8];
        bool valids[8] = {false, false, false, false, false, false, false,
                          false};
        for (int i = 0; i < 8; ++i) {
            if (!parse_field(fields[i].text, values[i], valids[i])) {
                ++out_.bad_record_count_;
                return;
            }
        }
        auto scaled = [&](int i) {
            return TMRange{values[i].min_ * k, values[i].max_ * k};
        };
        if (valids[0]) {
            e.rise_arrival_ = scaled(0);
            e.set_rise_arrival();
        }
        if (valids[1]) {
            e.rise_slew_ = scaled(1);
            e.set_rise_slew();
        }
        if (valids[2]) {
            ++out_.dropped_source_res_count_;
        }
        if (valids[3]) {
            ++out_.dropped_slack_count_;
        }
        if (valids[4]) {
            e.fall_arrival_ = scaled(4);
            e.set_fall_arrival();
        }
        if (valids[5]) {
            e.fall_slew_ = scaled(5);
            e.set_fall_slew();
        }
        if (valids[6]) {
            ++out_.dropped_source_res_count_;
        }
        if (valids[7]) {
            ++out_.dropped_slack_count_;
        }
        out_.upsert_timing(std::move(e));
    }

    // 记录边界恢复：消费至本记录 '(' 闭合（调用时恰有一个未闭合 '('）
    void recover_record() { skip_construct(1); }

    TMTokenReader reader_;
    TMTimingFile out_;
};

}  // namespace

TMTimingFile tm_parse_twf_text(const CMString& text,
                               const CMString& source_name) {
    TWFParser parser;
    return parser.parse(text, source_name);
}

TMTimingFile tm_parse_twf_file(const CMString& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("TWF: file not readable: " + path);
    }
    CMString text((std::istreambuf_iterator<char>(in)),
                  std::istreambuf_iterator<char>());
    return tm_parse_twf_text(text, path);
}

}  // namespace fly
