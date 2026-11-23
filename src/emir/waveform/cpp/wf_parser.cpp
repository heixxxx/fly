#include <emir/waveform/cpp/wf_parser.h>

#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>

namespace fly {
namespace {

// —— 词法层：token = 空白分隔的裸原子（VCD 无引号字符串；$date/$version
// 自由文本段按 token 收集至 $end）——
class VcdReader {
public:
    VcdReader() = default;

    void reset(const char* data, size_t size) {
        data_ = data;
        size_ = size;
        pos_ = 0;
        line_ = 1;
        has_pending_ = false;
    }

    // 读取下一 token；流尽返回 false。start = token 起始字节偏移（块界
    // 判定用），line = token 起始行（报错定位用）
    bool next(CMString& tok, size_t& start, size_t& line) {
        if (has_pending_) {
            tok = std::move(pending_tok_);
            start = pending_start_;
            line = pending_line_;
            has_pending_ = false;
            return true;
        }
        tok.clear();
        while (pos_ < size_ && is_space(data_[pos_])) {
            if (data_[pos_] == '\n') {
                ++line_;
            }
            ++pos_;
        }
        if (pos_ >= size_) {
            return false;
        }
        start = pos_;
        line = line_;
        while (pos_ < size_ && !is_space(data_[pos_])) {
            tok.push_back(data_[pos_]);
            ++pos_;
        }
        return true;
    }

    bool next(CMString& tok, size_t& line) {
        size_t start = 0;
        return next(tok, start, line);
    }

    bool next(CMString& tok) {
        size_t start = 0;
        size_t line = 0;
        return next(tok, start, line);
    }

    // 单槽回退（$dump 系关键字的独立 '(' 参数 token 预读判定）
    void push_back(const CMString& tok, size_t start, size_t line) {
        pending_tok_ = tok;
        pending_start_ = start;
        pending_line_ = line;
        has_pending_ = true;
    }

    size_t line() const { return line_; }

private:
    static bool is_space(char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' ||
               c == '\v';
    }

    const char* data_ = nullptr;
    size_t size_ = 0;
    size_t pos_ = 0;
    size_t line_ = 1;
    CMString pending_tok_;
    size_t pending_start_ = 0;
    size_t pending_line_ = 0;
    bool has_pending_ = false;
};

[[noreturn]] void stream_error(const CMString& what, size_t line) {
    throw std::runtime_error("VCD: " + what + " at line " +
                             std::to_string(line));
}

// 标量取值/档位换算/转储参数词法 = wf_types 公共单点（裁定 IV-5：
// wf_scalar_value / wf_timescale_unit_fs / wf_parse_timescale /
// wf_dump_keyword，与块扫描器共用）

// 位区间 token：[left:right] 或 [i]（单值 = left = right；负下标合法）
bool parse_range(const CMString& tok, int64_t& left, int64_t& right) {
    if (tok.size() < 3 || tok[0] != '[' || tok.back() != ']') {
        return false;
    }
    const CMString body = tok.substr(1, tok.size() - 2);
    const size_t colon = body.find(':');
    auto parse_i64 = [](const CMString& s, int64_t& v) {
        if (s.empty()) {
            return false;
        }
        char* endptr = nullptr;
        const long long x = std::strtoll(s.c_str(), &endptr, 10);
        if (endptr == s.c_str() || *endptr != '\0') {
            return false;
        }
        v = static_cast<int64_t>(x);
        return true;
    };
    if (colon == CMString::npos) {
        if (!parse_i64(body, left)) {
            return false;
        }
        right = left;
        return true;
    }
    return parse_i64(body.substr(0, colon), left) &&
           parse_i64(body.substr(colon + 1), right);
}

// —— 语法层：单遍扫描（头部声明段 → 事件段）。kCollect 收集轨道事件
//（整读解析，值等价过滤属解析语义）；kPlan 纯字节切块（裁定 V-3）——
// 只认 #时间戳行边界与 $dumpvars 段位置，不维护值表、不产事件 ——
class VcdParser {
public:
    enum class Mode { kCollect, kPlan };

    VcdParser(Mode mode, uint64_t chunk_size, const CMString& source)
        : mode_(mode), chunk_size_(chunk_size) {
        out_.source_file_ = source;
        plan_.file_name_ = source;
    }

    void run(const CMString& text) {
        text_ = text;  // 保活（reader_ 持指针）
        reader_.reset(text_.data(), text_.size());
        parse_header();
        if (vars_.vars_.empty()) {
            stream_error("no $var declarations before $enddefinitions",
                         reader_.line());
        }
        parse_events();
        if (mode_ == Mode::kCollect) {
            if (has_timestamp_) {
                out_.t_start_fs_ = first_fs_;
                out_.t_end_fs_ = last_fs_;
                out_.duration_fs_ = last_fs_ - first_fs_;
            }
            out_.vars_ = std::move(vars_);
            out_.rebuild_indexes();
        } else {
            plan_.file_size_ = text_.size();
            plan_.timescale_fs_ = out_.timescale_fs_;
            plan_.timescale_sec_ = out_.timescale_sec_;
            if (!plan_.chunks_.empty()) {
                plan_.chunks_.back().end_ = text_.size();  // 末块收口
            } else if (dumpvars_start_ != kNoOffset) {
                // 无 # 时间戳行的纯 $dumpvars 形态：单块兜底（块区间 =
                // $dumpvars 段起至文件尾，时间范围 [0,0]——初值事件无
                // 时间轴），初值事件照常入库——不退化为 0 块任务（与整
                // 读解析行为一致，裁定 IV-6 同源）
                WFFileChunk chunk;
                chunk.start_ = dumpvars_start_;
                chunk.end_ = text_.size();
                chunk.t_start_fs_ = 0;
                chunk.t_end_fs_ = 0;
                plan_.chunks_.push_back(std::move(chunk));
            }
            vars_.rebuild_indexes();
        }
    }

    WFVcdFile out_;          // kCollect 产物
    WFFileChunkPlan plan_;   // kPlan 产物
    WFVarTable vars_;        // 两模式产出（kCollect 随 out_ 移交）

private:
    void parse_header() {
        CMString tok;
        size_t start = 0;
        size_t line = 0;
        for (;;) {
            if (!reader_.next(tok, start, line)) {
                stream_error("missing $enddefinitions (unexpected end of "
                             "header)",
                             reader_.line());
            }
            if (tok == "$enddefinitions") {
                plan_.header_end_ = start;
                if (!has_timescale_) {
                    stream_error("missing $timescale (header incomplete)",
                                 line);
                }
                expect_end("enddefinitions", line);
                if (!scope_stack_.empty()) {
                    stream_error("unbalanced $scope at $enddefinitions",
                                 line);
                }
                return;
            }
            if (tok == "$date") {
                out_.date_ = collect_text("$date", line);
            } else if (tok == "$version") {
                out_.version_ = collect_text("$version", line);
            } else if (tok == "$timescale") {
                parse_timescale(line);
            } else if (tok == "$scope") {
                parse_scope(line);
            } else if (tok == "$upscope") {
                expect_end("upscope", line);
                if (scope_stack_.empty()) {
                    stream_error("$upscope without matching $scope", line);
                }
                scope_stack_.pop_back();
            } else if (tok == "$var") {
                parse_var(line);
            } else if (tok == "$comment") {
                skip_to_end();
            } else {
                // 其余头部构造静默跳过 + 计数（PROGRAM/DUMPALLS 等）
                skip_to_end();
                ++out_.unknown_construct_count_;
            }
        }
    }

    // $timescale 值域 1|10|100 × s|ms|us|ns|ps|fs；数字与单位可能分属两
    // token（Icarus 三行形态）——收集至 $end 后拼接解析。缺失或非法 =
    // 头部形态错（流级 raise——入口嗅探同口径，plan §3.1）
    void parse_timescale(size_t line) {
        CMString joined;
        size_t l = line;
        for (;;) {
            CMString tok;
            if (!reader_.next(tok, l)) {
                stream_error("unterminated $timescale", reader_.line());
            }
            if (tok == "$end") {
                break;
            }
            if (!joined.empty()) {
                joined += ' ';
            }
            joined += tok;
        }
        apply_timescale(joined, line);
    }

    // 数字与单位解析：档位 = 1|10|100 × s|ms|us|ns|ps|fs（词法公共单点
    // wf_parse_timescale；数字与单位间空格容差）。非法 = 头部形态错
    void apply_timescale(const CMString& joined, size_t line) {
        uint64_t fs = 0;
        double sec = 0.0;
        if (!wf_parse_timescale(joined, fs, sec)) {
            stream_error("invalid $timescale: " + joined, line);
        }
        out_.timescale_fs_ = fs;
        out_.timescale_sec_ = sec;
        has_timescale_ = true;
    }

    // $date/$version 自由文本段：token 序列收集至 $end（空格拼接——原文
    // 语义保留、解析器不消费）
    CMString collect_text(const char* kw, size_t line) {
        CMString joined;
        size_t l = line;
        for (;;) {
            CMString tok;
            if (!reader_.next(tok, l)) {
                stream_error(CMString("unterminated ") + kw, reader_.line());
            }
            if (tok == "$end") {
                break;
            }
            if (!joined.empty()) {
                joined += ' ';
            }
            joined += tok;
        }
        return joined;
    }

    void expect_end(const char* kw, size_t line) {
        CMString tok;
        size_t l = 0;
        if (!reader_.next(tok, l)) {
            stream_error(CMString("unterminated $") + kw, reader_.line());
        }
        if (tok != "$end") {
            stream_error(CMString("$") + kw + ": expected $end", l);
        }
    }

    void parse_scope(size_t line) {
        // $scope <type> <name> $end；type 值不消费（module 等仅观测），
        // 名字 token 序拼接（转义标识符空格容差）
        CMString tok;
        CMString name;
        bool have_type = false;
        size_t l = line;
        for (;;) {
            if (!reader_.next(tok, l)) {
                stream_error("unterminated $scope", reader_.line());
            }
            if (tok == "$end") {
                break;
            }
            if (!have_type) {
                have_type = true;
                continue;
            }
            if (!name.empty()) {
                name += ' ';
            }
            name += tok;
        }
        if (!have_type || name.empty()) {
            stream_error("$scope missing name", line);
        }
        scope_stack_.push_back(std::move(name));
    }

    void parse_var(size_t line) {
        // $var <type> <width> <id> <name> [range] $end
        CMString tok;
        size_t l = line;
        if (!reader_.next(tok, l)) {
            stream_error("unterminated $var", reader_.line());
        }
        const WFVarKind kind = wf_parse_var_kind(tok);
        if (!reader_.next(tok, l)) {
            stream_error("unterminated $var", reader_.line());
        }
        char* endptr = nullptr;
        const unsigned long long width =
            std::strtoull(tok.c_str(), &endptr, 10);
        if (endptr == tok.c_str() || *endptr != '\0' || width == 0 ||
            width > 0xFFFFFFFFULL) {
            ++out_.bad_record_count_;
            skip_to_end();
            return;
        }
        if (!reader_.next(tok, l)) {
            stream_error("unterminated $var", reader_.line());
        }
        CMString id = tok;
        // 名字收集（转义名 token 空格拼接）+ 可选位区间（'[' 起始 token）
        CMString name;
        bool has_range = false;
        int64_t range_left = 0;
        int64_t range_right = 0;
        for (;;) {
            if (!reader_.next(tok, l)) {
                stream_error("unterminated $var", reader_.line());
            }
            if (tok == "$end") {
                break;
            }
            if (tok[0] == '[') {
                if (has_range || !parse_range(tok, range_left, range_right)) {
                    ++out_.bad_record_count_;
                    skip_to_end();
                    return;
                }
                has_range = true;
                continue;
            }
            if (has_range) {
                // 区间后杂散 token = 破损声明
                ++out_.bad_record_count_;
                skip_to_end();
                return;
            }
            if (!name.empty()) {
                name += ' ';
            }
            name += tok;
        }
        if (name.empty()) {
            ++out_.bad_record_count_;
            return;
        }
        if (!wf_var_kind_supported(kind)) {
            // real/event 不支持功耗语义：跳过 + 计数（WFMG::0012）
            ++out_.unsupported_var_count_;
            return;
        }
        // 全路径名（scope 栈 '/' 连接 + var 名；根 scope 保留——剥除属
        // 换算层职责）
        CMString full;
        for (const CMString& seg : scope_stack_) {
            full += seg;
            full += '/';
        }
        full += name;
        // id 轨道登记（首现建轨道；同 id 宽度冲突 = 破损声明跳过计数）。
        // 纯切块模式（kPlan）不建值轨道事件表/当前值表——T1 无事件职责
        //（裁定 V-3）
        const auto found = track_of_id_.find(id);
        if (found == track_of_id_.end()) {
            track_of_id_.emplace(id, current_.size());
            vars_.track_ids_.push_back(id);
            vars_.track_widths_.push_back(static_cast<uint32_t>(width));
            if (mode_ == Mode::kCollect) {
                current_.push_back(CMVector<WFValue>(width, WFValue::X));
                WFTrackEvents track;
                track.id_code_ = id;
                track.width_ = static_cast<uint32_t>(width);
                track.bits_.resize(width);
                out_.tracks_.push_back(std::move(track));
            } else {
                current_.emplace_back();  // 槽位对齐（kPlan 不维护值）
            }
        } else if (vars_.track_widths_[found->second] != width) {
            ++out_.bad_record_count_;
            return;
        }
        WFVar var;
        var.id_code_ = std::move(id);
        var.name_ = std::move(full);
        var.width_ = static_cast<uint32_t>(width);
        var.has_range_ = has_range;
        var.range_left_ = range_left;
        var.range_right_ = range_right;
        var.kind_ = kind;
        vars_.vars_.push_back(std::move(var));
    }

    void parse_events() {
        CMString tok;
        size_t start = 0;
        size_t line = 0;
        for (;;) {
            if (!reader_.next(tok, start, line)) {
                return;  // 流尽 = 正常收尾（块表按已开块收口）
            }
            if (tok[0] == '#' && tok.size() > 1) {
                parse_timestamp(tok, start, line);
            } else if (tok[0] == '$') {
                parse_keyword(tok, start, line);
            } else {
                parse_value_record(tok, line, nullptr);
            }
        }
    }

    void parse_timestamp(const CMString& tok, size_t start, size_t line) {
        char* endptr = nullptr;
        const unsigned long long ts =
            std::strtoull(tok.c_str() + 1, &endptr, 10);
        if (endptr == tok.c_str() + 1 || *endptr != '\0') {
            stream_error("invalid timestamp token: " + tok, line);
        }
        // 档位换算 fs（uint64 乘法；乘前上界校验——档位 × 时间戳组合
        // 溢出属病态输入，流级 raise 防静默回绕）
        if (ts > UINT64_MAX / out_.timescale_fs_) {
            stream_error("timestamp overflow after timescale scaling: " +
                             tok, line);
        }
        const uint64_t fs = static_cast<uint64_t>(ts) * out_.timescale_fs_;
        if (has_timestamp_ && fs < last_fs_) {
            stream_error("timestamp regress (non-monotonic)", line);
        }
        if (!has_timestamp_) {
            first_fs_ = fs;
            has_timestamp_ = true;
        }
        last_fs_ = fs;
        now_fs_ = fs;
        if (mode_ != Mode::kPlan) {
            return;
        }
        // 块界 = #时间戳行边界（裁定 V-3 纯字节切块——无快照产出）：首
        // 块起点 = min($dumpvars 段起点, 首个 # 行起点)——初值事件段不
        // 可丢失（裁定 IV-6）；chunk_size 字节界后的时间戳行收口前块、
        // 开新块（plan §5.1/§6 T1 口）
        if (plan_.chunks_.empty()) {
            WFFileChunk chunk;
            chunk.start_ = start;
            if (dumpvars_start_ != kNoOffset && dumpvars_start_ < start) {
                chunk.start_ = dumpvars_start_;
            }
            chunk.t_start_fs_ = fs;
            chunk.t_end_fs_ = fs;
            plan_.chunks_.push_back(std::move(chunk));
        } else if (start - plan_.chunks_.back().start_ >= chunk_size_) {
            plan_.chunks_.back().end_ = start;  // 前块收口（块间衔接）
            WFFileChunk chunk;
            chunk.start_ = start;
            chunk.t_start_fs_ = fs;
            chunk.t_end_fs_ = fs;
            plan_.chunks_.push_back(std::move(chunk));
        }
        plan_.chunks_.back().t_end_fs_ = fs;
    }

    void parse_keyword(const CMString& tok, size_t start, size_t line) {
        // $keyword 可带括号参数后缀（$dumpvars(1, top) 单 token 形态）：
        // 词法公共单点 wf_dump_keyword 剥参数得关键字本体；参数闭合 ')'
        // 不在原 token 内则续扫至含 ')' 的 token
        CMString kw;
        bool scan_args = false;
        const bool is_dump = wf_dump_keyword(tok, kw, scan_args);
        if (scan_args) {
            consume_args(line);
        } else if (is_dump && tok.find('(') == CMString::npos) {
            // '(' 参数独立 token 形态（$dumpvars (1, top)）——预读判定，
            // 非参数 token 回退
            CMString t;
            size_t s = 0;
            size_t l = 0;
            if (reader_.next(t, s, l)) {
                if (t[0] == '(') {
                    consume_args_from(t, l);
                } else {
                    reader_.push_back(t, s, l);
                }
            }
        }
        if (kw == "$dumpvars" && dumpvars_start_ == kNoOffset) {
            dumpvars_start_ = start;  // 首块起点判定材料（裁定 IV-6）
        }
        if (is_dump) {
            parse_dump_section(kw == "$dumpoff", line);
        } else if (kw == "$comment") {
            skip_to_end();
        } else if (kw == "$end") {
            // 无所属段的杂散 $end：条目级跳过计数
            ++out_.bad_record_count_;
        } else {
            skip_to_end();
            ++out_.unknown_construct_count_;
        }
    }

    void consume_args(size_t line) {
        CMString t;
        size_t s = 0;
        size_t l = 0;
        for (;;) {
            if (!reader_.next(t, s, l)) {
                stream_error("unterminated construct argument",
                             reader_.line());
            }
            if (t.find(')') != CMString::npos) {
                return;
            }
        }
    }

    void consume_args_from(const CMString& first, size_t line) {
        if (first.find(')') != CMString::npos) {
            return;
        }
        consume_args(line);
    }

    // $dump 段：段内值行照常解析；$dumpoff 语义 = 全值轨道置 x——段内
    // 未显式出现的轨道合成 x 事件（IEEE $dumpoff 全 x 语义；段内显式行
    // 与合成置位均走值等价过滤）
    void parse_dump_section(bool is_off, size_t line) {
        CMUnorderedSet<uint32_t> touched;
        CMString tok;
        size_t s = 0;
        size_t l = 0;
        for (;;) {
            if (!reader_.next(tok, s, l)) {
                stream_error("unterminated $dump section", reader_.line());
            }
            if (tok == "$end") {
                break;
            }
            if (tok[0] == '$') {
                skip_to_end();
                ++out_.unknown_construct_count_;
                continue;
            }
            parse_value_record(tok, l, is_off && mode_ == Mode::kCollect
                                             ? &touched
                                             : nullptr);
        }
        // $dumpoff 全轨道合成置 x 仅整读解析语义（当前值表依赖）；纯切块
        // 模式（kPlan）无值表、无事件职责（裁定 V-3）
        if (!is_off || mode_ != Mode::kCollect) {
            return;
        }
        for (size_t t = 0; t < current_.size(); ++t) {
            if (touched.count(static_cast<uint32_t>(t)) != 0) {
                continue;
            }
            for (size_t b = 0; b < current_[t].size(); ++b) {
                if (current_[t][b] == WFValue::X) {
                    continue;
                }
                current_[t][b] = WFValue::X;
                out_.tracks_[t].bits_[b].push_back(
                    WFEvent{now_fs_, WFValue::X});
            }
        }
    }

    // 值变化记录：标量 0! / 向量 b1010 *（最高位在前）/ real r 前缀与
    // EVCD 强度（S）/port（p）记录（不支持构造——跳过 + 计数）。纯切块
    // 模式仅按记录形态消费 token（含两 token 形态的 id），不施值——T1
    // 无事件职责（裁定 V-3）
    void parse_value_record(const CMString& tok, size_t line,
                            CMUnorderedSet<uint32_t>* touched) {
        const char c = tok[0];
        WFValue v = WFValue::X;
        if (wf_scalar_value(c, v)) {
            CMString id = tok.substr(1);
            if (id.empty() && !next_token(id)) {
                ++out_.bad_record_count_;
                return;
            }
            if (mode_ == Mode::kPlan) {
                return;
            }
            apply_scalar(id, v, touched);
            return;
        }
        if (c == 'b' || c == 'B') {
            CMString bits = tok.substr(1);
            if (bits.empty() && !next_token(bits)) {
                ++out_.bad_record_count_;
                return;
            }
            CMString id;
            if (!next_token(id)) {
                ++out_.bad_record_count_;
                return;
            }
            if (mode_ == Mode::kPlan) {
                return;
            }
            apply_vector(bits, id, touched);
            return;
        }
        if (c == 'r' || c == 'R' || c == 's' || c == 'S' || c == 'p' ||
            c == 'P') {
            // real 值行 / EVCD 强度·port 记录：按「记录 token + 邻随 id
            // token」消费跳过（WFMG::0012；kPlan 下计数随丢弃的 out_ 消
            // 失——计划不承载计数）
            CMString id;
            if (!next_token(id)) {
                ++out_.bad_record_count_;
                return;
            }
            ++out_.unsupported_record_count_;
            return;
        }
        (void)line;
        ++out_.bad_record_count_;  // 未知记录形态
    }

    bool next_token(CMString& tok) {
        size_t s = 0;
        size_t l = 0;
        return reader_.next(tok, s, l);
    }

    void apply_scalar(const CMString& id, WFValue v,
                      CMUnorderedSet<uint32_t>* touched) {
        const auto found = track_of_id_.find(id);
        if (found == track_of_id_.end()) {
            ++out_.bad_record_count_;  // 未知 id 码
            return;
        }
        const size_t t = found->second;
        if (touched != nullptr) {
            touched->insert(static_cast<uint32_t>(t));
        }
        // 全位同值判定（裁定 IV-7）：多位向量简写行的等价判定必须覆盖全
        // 位——仅查位 0 会漏置高位（构造 [x,0] 态后施 x 简写行的形态）
        bool all_same = true;
        for (size_t b = 0; b < current_[t].size(); ++b) {
            if (current_[t][b] != v) {
                all_same = false;
                break;
            }
        }
        if (all_same) {
            // README 特征 4：重复值记录行（组合毛刺同刻聚合）——不产事
            // 件，观测计数
            ++out_.duplicate_value_count_;
            return;
        }
        // 标量速记施于向量轨道 = 全位同值（IEEE 值等价口径；真实工具
        // $dumpoff 段向量轨道以 x 标量行出现的形态）
        for (size_t b = 0; b < current_[t].size(); ++b) {
            if (current_[t][b] == v) {
                continue;
            }
            current_[t][b] = v;
            out_.tracks_[t].bits_[b].push_back(WFEvent{now_fs_, v});
        }
    }

    void apply_vector(const CMString& bits, const CMString& id,
                      CMUnorderedSet<uint32_t>* touched) {
        const auto found = track_of_id_.find(id);
        if (found == track_of_id_.end()) {
            ++out_.bad_record_count_;
            return;
        }
        const size_t t = found->second;
        const uint32_t width = static_cast<uint32_t>(current_[t].size());
        for (char ch : bits) {
            WFValue dummy = WFValue::X;
            if (!wf_scalar_value(ch, dummy)) {
                ++out_.bad_record_count_;  // 非法位串字符
                return;
            }
        }
        if (bits.size() > width) {
            ++out_.bad_record_count_;  // 位串超宽
            return;
        }
        if (touched != nullptr) {
            touched->insert(static_cast<uint32_t>(t));
        }
        // 最高位在前 + LSB 右对齐：值串最右字符 = 位权 2^0 位（短值左
        // 侧补 0——Icarus 实测 b1 = 0001 形态）；值等价过滤仅变化位产
        // 事件（plan §2）
        bool changed = false;
        for (uint32_t b = 0; b < width; ++b) {
            WFValue v = WFValue::ZERO;
            if (b < bits.size()) {
                wf_scalar_value(bits[bits.size() - 1 - b], v);
            }
            if (current_[t][b] == v) {
                continue;
            }
            current_[t][b] = v;
            changed = true;
            out_.tracks_[t].bits_[b].push_back(WFEvent{now_fs_, v});
        }
        if (!changed) {
            // README 特征 5：位值等价的重复向量记录
            ++out_.duplicate_value_count_;
        }
    }

    void skip_to_end() {
        CMString tok;
        size_t l = 0;
        for (;;) {
            if (!reader_.next(tok, l)) {
                stream_error("unterminated construct (unexpected end of "
                             "stream)",
                             reader_.line());
            }
            if (tok == "$end") {
                return;
            }
        }
    }

    Mode mode_;
    uint64_t chunk_size_;
    CMString text_;   // 保活（reader_ 持指针）
    VcdReader reader_;
    CMUnorderedMap<CMString, size_t> track_of_id_;  // id 码 → 轨道下标
                                                    //（解析期增量索引）
    CMVector<CMVector<WFValue>> current_;  // 轨道逐位当前值（kCollect 等
                                           // 价过滤源；kPlan 不维护值——
                                           // 与 track_ids_ 平行的空槽）
    CMVector<CMString> scope_stack_;
    uint64_t now_fs_ = 0;        // 事件段时间游标（fs）
    bool has_timestamp_ = false;
    bool has_timescale_ = false;  // $timescale 头部存在标记（缺失 = 头部
                                  // 形态错，流级 raise）
    uint64_t first_fs_ = 0;
    uint64_t last_fs_ = 0;
    // 事件段首个 $dumpvars 关键字字节偏移（首块起点判定材料——裁定
    // IV-6；kNoOffset = 未出现）
    uint64_t dumpvars_start_ = kNoOffset;

    static constexpr uint64_t kNoOffset = UINT64_MAX;
};

// gzip 魔数嗅探（0x1f 0x8b）——流程层入口已解压（裁定 IV-1），直连
// C++ API 不支持压缩输入，明确报错指引解压
void reject_gzip(const CMString& text, const CMString& source) {
    if (text.size() >= 2 && static_cast<unsigned char>(text[0]) == 0x1f &&
        static_cast<unsigned char>(text[1]) == 0x8b) {
        throw std::runtime_error(
            "VCD: gzip compressed input not supported by direct C++ API "
            "(flow layer decompresses first), decompress first: " + source);
    }
}

CMString read_file_or_throw(const CMString& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("VCD: file not readable: " + path);
    }
    return CMString((std::istreambuf_iterator<char>(in)),
                    std::istreambuf_iterator<char>());
}

}  // namespace

WFVcdFile wf_parse_vcd_text(const CMString& text,
                            const CMString& source_name) {
    VcdParser parser(VcdParser::Mode::kCollect, 0, source_name);
    parser.run(text);
    return std::move(parser.out_);
}

WFVcdFile wf_parse_vcd_file(const CMString& path) {
    const CMString text = read_file_or_throw(path);
    reject_gzip(text, path);
    return wf_parse_vcd_text(text, path);
}

WFFileChunkPlan wf_plan_file_chunks(const CMString& path, uint64_t chunk_size,
                                    WFVarTable& vars_out) {
    const CMString text = read_file_or_throw(path);
    reject_gzip(text, path);
    VcdParser parser(VcdParser::Mode::kPlan, chunk_size, path);
    parser.run(text);
    vars_out = std::move(parser.vars_);
    return std::move(parser.plan_);
}

}  // namespace fly
