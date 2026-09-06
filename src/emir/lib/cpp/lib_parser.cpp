#include <emir/lib/cpp/lib_parser.h>

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" {
#include <si2dr_liberty.h>
}

namespace fly {

namespace {

// ── si2dr 基础访问辅助（迭代器模式：Next* 前进并返回 IdT，Null 判止，
//    用毕 IterQuit——与解析器自带 main.c 的用法一致）─────────────────

CMString group_first_name(si2drGroupIdT group) {
    si2drErrorT err = SI2DR_NO_ERROR;
    si2drNamesIdT names = si2drGroupGetNames(group, &err);
    si2drStringT name = si2drIterNextName(names, &err);
    CMString out = name ? name : "";
    si2drIterQuit(names, &err);
    return out;
}

// 组类型名（si2drGroupGetGroupType 直接返回名字符串，如 "pin"/"internal_power"）
CMString group_type_name(si2drGroupIdT group) {
    si2drErrorT err = SI2DR_NO_ERROR;
    si2drStringT t = si2drGroupGetGroupType(group, &err);
    return t ? t : "";
}

// 遍历 group 的全部属性，逐个回调
template <typename Fn>
void for_each_attr(si2drGroupIdT group, Fn&& fn) {
    si2drErrorT err = SI2DR_NO_ERROR;
    si2drAttrsIdT attrs = si2drGroupGetAttrs(group, &err);
    si2drAttrIdT attr;
    while (!si2drObjectIsNull((attr = si2drIterNextAttr(attrs, &err)), &err)) {
        fn(attr);
    }
    si2drIterQuit(attrs, &err);
}

// 遍历 group 的全部子组，逐个回调（带组类型名）
template <typename Fn>
void for_each_group(si2drGroupIdT group, Fn&& fn) {
    si2drErrorT err = SI2DR_NO_ERROR;
    si2drGroupsIdT groups = si2drGroupGetGroups(group, &err);
    si2drGroupIdT sub;
    while (!si2drObjectIsNull((sub = si2drIterNextGroup(groups, &err)), &err)) {
        fn(sub, group_type_name(sub));
    }
    si2drIterQuit(groups, &err);
}

// simple 属性值 → 文本
CMString simple_attr_value_text(si2drAttrIdT attr) {
    si2drErrorT err = SI2DR_NO_ERROR;
    switch (si2drSimpleAttrGetValueType(attr, &err)) {
        case SI2DR_FLOAT64: {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%g", si2drSimpleAttrGetFloat64Value(attr, &err));
            return buf;
        }
        case SI2DR_INT32: {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%d", si2drSimpleAttrGetInt32Value(attr, &err));
            return buf;
        }
        case SI2DR_STRING: {
            si2drStringT s = si2drSimpleAttrGetStringValue(attr, &err);
            return s ? s : "";
        }
        case SI2DR_BOOLEAN:
            return si2drSimpleAttrGetBooleanValue(attr, &err) ? "true" : "false";
        default:
            return "";
    }
}

// complex 属性值遍历辅助：参考实现的头文件虽声明句柄式 API
// （si2drIterNextComplex 等），但 PI.c 未实现——实际入口是 8 参数的
// si2drIterNextComplexValue，以 SI2DR_UNDEFINED_VALUETYPE 终止。
template <typename Fn>
void for_each_complex_value(si2drAttrIdT attr, Fn&& fn) {
    si2drErrorT err = SI2DR_NO_ERROR;
    si2drValuesIdT vals = si2drComplexAttrGetValues(attr, &err);
    while (true) {
        si2drValueTypeT type = SI2DR_UNDEFINED_VALUETYPE;
        si2drInt32T iv = 0;
        si2drFloat64T dv = 0.0;
        si2drStringT sv = nullptr;
        si2drBooleanT bv = SI2DR_FALSE;
        si2drExprT* ev = nullptr;
        si2drIterNextComplexValue(vals, &type, &iv, &dv, &sv, &bv, &ev, &err);
        if (type == SI2DR_UNDEFINED_VALUETYPE) break;
        fn(type, iv, dv, sv, bv);
    }
    si2drIterQuit(vals, &err);
}

// complex 属性值列表 → 逗号连接文本（全量保存的兜底形态）
CMString complex_attr_value_text(si2drAttrIdT attr) {
    CMString out;
    bool first = true;
    for_each_complex_value(attr, [&](si2drValueTypeT type, si2drInt32T iv,
                                     si2drFloat64T dv, si2drStringT sv,
                                     si2drBooleanT bv) {
        char buf[64];
        if (!first) out += ",";
        first = false;
        switch (type) {
            case SI2DR_FLOAT64:
                std::snprintf(buf, sizeof(buf), "%g", dv);
                out += buf;
                break;
            case SI2DR_INT32:
                std::snprintf(buf, sizeof(buf), "%d", iv);
                out += buf;
                break;
            case SI2DR_STRING:
                if (sv) out += sv;
                break;
            case SI2DR_BOOLEAN:
                out += bv ? "true" : "false";
                break;
            default:
                break;
        }
    });
    return out;
}

CMString attr_value_text(si2drAttrIdT attr) {
    si2drErrorT err = SI2DR_NO_ERROR;
    switch (si2drAttrGetAttrType(attr, &err)) {
        case SI2DR_SIMPLE:  return simple_attr_value_text(attr);
        case SI2DR_COMPLEX: return complex_attr_value_text(attr);
        default:            return "";
    }
}

// simple string 属性读取（找不到返回 false）
bool find_string_attr(si2drGroupIdT group, const char* name, CMString& out) {
    bool found = false;
    for_each_attr(group, [&](si2drAttrIdT attr) {
        si2drErrorT err = SI2DR_NO_ERROR;
        si2drStringT n = si2drAttrGetName(attr, &err);
        if (!found && n && std::strcmp(n, name) == 0 &&
            si2drAttrGetAttrType(attr, &err) == SI2DR_SIMPLE) {
            out = simple_attr_value_text(attr);
            found = true;
        }
    });
    return found;
}

bool find_float_attr(si2drGroupIdT group, const char* name, double& out) {
    bool found = false;
    for_each_attr(group, [&](si2drAttrIdT attr) {
        si2drErrorT err = SI2DR_NO_ERROR;
        si2drStringT n = si2drAttrGetName(attr, &err);
        if (!found && n && std::strcmp(n, name) == 0 &&
            si2drAttrGetAttrType(attr, &err) == SI2DR_SIMPLE &&
            si2drSimpleAttrGetValueType(attr, &err) == SI2DR_FLOAT64) {
            out = si2drSimpleAttrGetFloat64Value(attr, &err);
            found = true;
        }
    });
    return found;
}

// complex float 列表属性（index_1/2/3、values）→ double 向量。
// Liberty 的数值列表以「引号字符串」承载（如 values ("1.0, 1.1", "2.0")——
// 每个 STRING 值内含逗号分隔的多个数值），须拆分解析；个别裸 FLOAT64 值
// 直接收取。
CMVector<double> float_list_attr(si2drGroupIdT group, const char* name) {
    CMVector<double> out;
    for_each_attr(group, [&](si2drAttrIdT attr) {
        si2drErrorT err = SI2DR_NO_ERROR;
        si2drStringT n = si2drAttrGetName(attr, &err);
        if (!n || std::strcmp(n, name) != 0) return;
        if (si2drAttrGetAttrType(attr, &err) != SI2DR_COMPLEX) return;
        for_each_complex_value(attr, [&](si2drValueTypeT type, si2drInt32T,
                                         si2drFloat64T dv, si2drStringT sv,
                                         si2drBooleanT) {
            if (type == SI2DR_FLOAT64) {
                out.push_back(dv);
            } else if (type == SI2DR_STRING && sv) {
                // 引号串：逗号分隔数值列表，逐段 strtod
                const char* p = sv;
                while (*p) {
                    char* end = nullptr;
                    double v = std::strtod(p, &end);
                    if (end == p) {
                        ++p;              // 跳过逗号/空白等非数值字符
                    } else {
                        out.push_back(v);
                        p = end;
                    }
                }
            }
        });
    });
    return out;
}

bool group_has_values_attr(si2drGroupIdT group) {
    bool found = false;
    for_each_attr(group, [&](si2drAttrIdT attr) {
        si2drErrorT err = SI2DR_NO_ERROR;
        si2drStringT n = si2drAttrGetName(attr, &err);
        if (!found && n && std::strcmp(n, "values") == 0 &&
            si2drAttrGetAttrType(attr, &err) == SI2DR_COMPLEX) {
            found = true;
        }
    });
    return found;
}

// ── 表 / 模板 / 组树提取 ───────────────────────────────────────────

// 表组（含 values complex 属性的组）→ CMLookupTable。
// Liberty 语义：表组类型 = 表语义名（rise_power/cell_rise/...），组名 =
// 引用的模板名；index_N 为表级索引覆盖（可缺省，缺省轴由模板提供）。
// 无任何表级索引时维度取引用模板的维度（查不到模板则置 1 兜底）。
CMLookupTable collect_table(si2drGroupIdT group, const CMString& type_name,
                            const LIBLibrary& lib) {
    CMLookupTable tbl;
    tbl.name_ = type_name;
    tbl.template_name_ = group_first_name(group);

    CMVector<double> i1 = float_list_attr(group, "index_1");
    CMVector<double> i2 = float_list_attr(group, "index_2");
    CMVector<double> i3 = float_list_attr(group, "index_3");
    CMVector<double> values = float_list_attr(group, "values");

    int dim = 0;
    if (!i3.empty()) dim = 3;
    else if (!i2.empty()) dim = 2;
    else if (!i1.empty()) dim = 1;
    else {
        dim = 1;
        const CMLookupTableTemplate* tmpl = lib.find_template(tbl.template_name_);
        if (tmpl) dim = static_cast<int>(tmpl->dim());
    }
    tbl.dim_ = dim;
    tbl.index_sets_.resize(dim);
    if (dim >= 1 && !i1.empty()) tbl.index_sets_[0] = i1;
    if (dim >= 2 && !i2.empty()) tbl.index_sets_[1] = i2;
    if (dim >= 3 && !i3.empty()) tbl.index_sets_[2] = i3;
    tbl.values_ = values;
    return tbl;
}

// 收集组下全部表组（含 values 属性的子组），未知子组统计
void collect_tables(si2drGroupIdT group, LIBLibrary& lib, CMVector<CMLookupTable>& out) {
    for_each_group(group, [&](si2drGroupIdT sub, const CMString& type_name) {
        if (group_has_values_attr(sub)) {
            out.push_back(collect_table(sub, type_name, lib));
        } else {
            lib.record_skipped_group(type_name.empty() ? "<unnamed>" : type_name);
        }
    });
}

void collect_internal_power(si2drGroupIdT group, LIBLibrary& lib, LIBInternalPower& ip) {
    find_string_attr(group, "related_pin", ip.related_pin_);
    for_each_attr(group, [&](si2drAttrIdT attr) {
        si2drErrorT err = SI2DR_NO_ERROR;
        si2drStringT n = si2drAttrGetName(attr, &err);
        if (!n) return;
        CMString name = n;
        if (name == "related_pin") return;
        LIBHeaderAttr a;
        a.name_ = name;
        a.value_ = attr_value_text(attr);
        if (!a.value_.empty()) ip.extra_attrs_.push_back(a);
    });
    collect_tables(group, lib, ip.tables_);
}

void collect_timing(si2drGroupIdT group, LIBLibrary& lib, LIBTimingArc& arc) {
    find_string_attr(group, "related_pin", arc.related_pin_);
    find_string_attr(group, "timing_sense", arc.timing_sense_);
    find_string_attr(group, "timing_type", arc.timing_type_);
    for_each_attr(group, [&](si2drAttrIdT attr) {
        si2drErrorT err = SI2DR_NO_ERROR;
        si2drStringT n = si2drAttrGetName(attr, &err);
        if (!n) return;
        CMString name = n;
        if (name == "related_pin" || name == "timing_sense" || name == "timing_type") return;
        LIBHeaderAttr a;
        a.name_ = name;
        a.value_ = attr_value_text(attr);
        if (!a.value_.empty()) arc.extra_attrs_.push_back(a);
    });
    collect_tables(group, lib, arc.tables_);
}

void collect_pin(si2drGroupIdT group, LIBLibrary& lib, LIBPin& pin) {
    pin.name_ = group_first_name(group);
    find_string_attr(group, "direction", pin.direction_);
    find_float_attr(group, "capacitance", pin.capacitance_);
    find_float_attr(group, "rise_capacitance", pin.rise_capacitance_);
    find_float_attr(group, "fall_capacitance", pin.fall_capacitance_);
    find_float_attr(group, "max_capacitance", pin.max_capacitance_);

    for_each_group(group, [&](si2drGroupIdT sub, const CMString& type_name) {
        if (type_name == "internal_power") {
            LIBInternalPower ip;
            collect_internal_power(sub, lib, ip);
            pin.internal_powers_.push_back(std::move(ip));
        } else if (type_name == "timing") {
            LIBTimingArc arc;
            collect_timing(sub, lib, arc);
            pin.timings_.push_back(std::move(arc));
        } else {
            // pin 级其他子组（pg_pin 等）：结构化收集留待需要时扩展
            lib.record_skipped_group(type_name.empty() ? "<unnamed>" : type_name);
        }
    });

    for_each_attr(group, [&](si2drAttrIdT attr) {
        si2drErrorT err = SI2DR_NO_ERROR;
        si2drStringT n = si2drAttrGetName(attr, &err);
        if (!n) return;
        CMString name = n;
        if (name == "direction" || name == "capacitance" ||
            name == "rise_capacitance" || name == "fall_capacitance" ||
            name == "max_capacitance") return;
        LIBHeaderAttr a;
        a.name_ = name;
        a.value_ = attr_value_text(attr);
        if (!a.value_.empty()) pin.extra_attrs_.push_back(a);
    });
}

void collect_cell(si2drGroupIdT group, LIBLibrary& lib, LIBCell& cell) {
    cell.name_ = group_first_name(group);
    find_float_attr(group, "area", cell.area_);

    for_each_group(group, [&](si2drGroupIdT sub, const CMString& type_name) {
        if (type_name == "pin") {
            LIBPin pin;
            collect_pin(sub, lib, pin);
            cell.pins_.push_back(std::move(pin));
        } else if (type_name == "ff" || type_name == "latch") {
            cell.is_sequence_cell_ = true;
        } else {
            // cell 级其他子组（burst/clkgate 等）：统计
            lib.record_skipped_group(type_name.empty() ? "<unnamed>" : type_name);
        }
    });

    for_each_attr(group, [&](si2drAttrIdT attr) {
        si2drErrorT err = SI2DR_NO_ERROR;
        si2drStringT n = si2drAttrGetName(attr, &err);
        if (!n || std::strcmp(n, "area") == 0) return;
        LIBHeaderAttr a;
        a.name_ = n;
        a.value_ = attr_value_text(attr);
        if (!a.value_.empty()) cell.extra_attrs_.push_back(a);
    });
}

// 模板组（类型名含 "template"）→ CMLookupTableTemplate
void collect_template(si2drGroupIdT group, LIBLibrary& lib) {
    CMLookupTableTemplate tmpl;
    CMVector<double> i1 = float_list_attr(group, "index_1");
    CMVector<double> i2 = float_list_attr(group, "index_2");
    CMVector<double> i3 = float_list_attr(group, "index_3");
    CMString v1, v2, v3;
    find_string_attr(group, "variable_1", v1);
    find_string_attr(group, "variable_2", v2);
    find_string_attr(group, "variable_3", v3);

    if (!v1.empty()) tmpl.variable_names_.push_back(v1);
    if (!v2.empty()) tmpl.variable_names_.push_back(v2);
    if (!v3.empty()) tmpl.variable_names_.push_back(v3);
    if (!i1.empty()) tmpl.index_sets_.push_back(i1);
    if (!i2.empty()) tmpl.index_sets_.push_back(i2);
    if (!i3.empty()) tmpl.index_sets_.push_back(i3);

    if (tmpl.variable_names_.size() == tmpl.index_sets_.size() &&
        !tmpl.variable_names_.empty()) {
        lib.template_names_.push_back(group_first_name(group));
        lib.templates_.push_back(std::move(tmpl));
    } else {
        lib.record_skipped_group("template_incomplete");
    }
}

void collect_library(si2drGroupIdT group, LIBLibrary& lib) {
    lib.name_ = group_first_name(group);

    for_each_group(group, [&](si2drGroupIdT sub, const CMString& type_name) {
        if (type_name.find("template") != CMString::npos) {
            collect_template(sub, lib);
        } else if (type_name == "cell" || type_name == "test_cell" ||
                   type_name == "scaled_cell") {
            LIBCell cell;
            collect_cell(sub, lib, cell);
            lib.cells_.push_back(std::move(cell));
        } else {
            // 库级其他子组（operating_conditions/wire_load/type 等）：统计
            lib.record_skipped_group(type_name.empty() ? "<unnamed>" : type_name);
        }
    });

    for_each_attr(group, [&](si2drAttrIdT attr) {
        si2drErrorT err = SI2DR_NO_ERROR;
        si2drStringT n = si2drAttrGetName(attr, &err);
        LIBHeaderAttr a;
        a.name_ = n ? n : "";
        a.value_ = attr_value_text(attr);
        if (!a.name_.empty() && !a.value_.empty()) lib.header_attrs_.push_back(a);
    });
}

}  // namespace

namespace {

// PI 会话 RAII：适配层任何退出路径（异常/正常）都清理 PI 内存库
struct PiSessionGuard {
    ~PiSessionGuard() {
        si2drErrorT e = SI2DR_NO_ERROR;
        si2drPIQuit(&e);
    }
};

}  // namespace

LIBLibrary lib_parse_lib_file(const CMString& path) {
    si2drErrorT err = SI2DR_NO_ERROR;

    // PI 内存库为进程级单例：Init 重建哈希/串表（上一文件的组已在此前
    // 解析结束时的 PIQuit 中清理），保证同 worker 进程顺序解析多文件互
    // 不串染。注意首次调用时 PI 尚未初始化，不可先 PIQuit（空表指针）。
    si2drPIInit(&err);
    PiSessionGuard guard;

    err = SI2DR_NO_ERROR;
    // 解析器接口吃可写 char*（main.c 先例）：给独立可写拷贝，不赌其只读。
    std::vector<char> writable_path(path.begin(), path.end());
    writable_path.push_back('\0');
    si2drReadLibertyFile(writable_path.data(), &err);
    if (err == SI2DR_INVALID_NAME) {
        throw std::runtime_error("lib_parse_lib_file: cannot open file: " + path);
    }
    if (err == SI2DR_SYNTAX_ERROR) {
        throw std::runtime_error("lib_parse_lib_file: syntax error in file: " + path);
    }
    if (err != SI2DR_NO_ERROR) {
        throw std::runtime_error("lib_parse_lib_file: parse failed (" +
                                 std::to_string(static_cast<int>(err)) + "): " + path);
    }

    LIBLibrary lib;
    si2drGroupsIdT groups = si2drPIGetGroups(&err);
    si2drGroupIdT group;
    while (!si2drObjectIsNull((group = si2drIterNextGroup(groups, &err)), &err)) {
        collect_library(group, lib);
    }
    si2drIterQuit(groups, &err);

    if (lib.cells_.empty()) {
        throw std::runtime_error("lib_parse_lib_file: no cell parsed from: " + path);
    }
    return lib;   // PI 内存库由 PiSessionGuard 析构清理
}

}  // namespace fly
