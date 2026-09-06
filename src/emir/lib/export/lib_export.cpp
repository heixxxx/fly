// _fly_emir_lib.so — emir lib 库 db 的 Python 绑定入口。
// 导出 LIBLibrary/LIBCell 等数据结构（FLY_EXPORT_SERIALIZE_PICKLE 支持
// pickle 落 db）与 lib_parse_lib_file 解析入口。
// 表数据（CMLookupTable）的完整绑定在 _fly_common；此处各结构对表集合
// 只暴露只读计数——表的数值消费在 C++ 计算引擎（power/current），Python
// 编排层仅需结构完备性可观测。
#include <export/cpp/export_macros.h>
#include <emir/lib/cpp/lib_parser.h>
#include <emir/lib/cpp/lib_types.h>
#include <nanobind/stl/vector.h>
#include <nanobind/stl/string.h>

FLY_EXPORT_MODULE(_fly_emir_lib) {

FLY_EXPORT_CLASS(fly::LIBHeaderAttr, "EXLIBHeaderAttr")
    FLY_EXPORT_INIT()
    FLY_EXPORT_ATTR("name", &fly::LIBHeaderAttr::name_)
    FLY_EXPORT_ATTR("value", &fly::LIBHeaderAttr::value_)
    FLY_EXPORT_SERIALIZE_PICKLE(fly::LIBHeaderAttr);

FLY_EXPORT_CLASS(fly::LIBInternalPower, "EXLIBInternalPower")
    FLY_EXPORT_INIT()
    FLY_EXPORT_ATTR("related_pin", &fly::LIBInternalPower::related_pin_)
    FLY_EXPORT_ATTR("extra_attrs", &fly::LIBInternalPower::extra_attrs_)
    FLY_EXPORT_READONLY_PROPERTY("table_count", [](const fly::LIBInternalPower& p) {
        return static_cast<int>(p.tables_.size());
    })
    FLY_EXPORT_SERIALIZE_PICKLE(fly::LIBInternalPower);

FLY_EXPORT_CLASS(fly::LIBTimingArc, "EXLIBTimingArc")
    FLY_EXPORT_INIT()
    FLY_EXPORT_ATTR("related_pin", &fly::LIBTimingArc::related_pin_)
    FLY_EXPORT_ATTR("timing_sense", &fly::LIBTimingArc::timing_sense_)
    FLY_EXPORT_ATTR("timing_type", &fly::LIBTimingArc::timing_type_)
    FLY_EXPORT_ATTR("extra_attrs", &fly::LIBTimingArc::extra_attrs_)
    FLY_EXPORT_READONLY_PROPERTY("table_count", [](const fly::LIBTimingArc& a) {
        return static_cast<int>(a.tables_.size());
    })
    FLY_EXPORT_SERIALIZE_PICKLE(fly::LIBTimingArc);

FLY_EXPORT_CLASS(fly::LIBPin, "EXLIBPin")
    FLY_EXPORT_INIT()
    FLY_EXPORT_ATTR("name", &fly::LIBPin::name_)
    FLY_EXPORT_ATTR("direction", &fly::LIBPin::direction_)
    FLY_EXPORT_ATTR("capacitance", &fly::LIBPin::capacitance_)
    FLY_EXPORT_ATTR("rise_capacitance", &fly::LIBPin::rise_capacitance_)
    FLY_EXPORT_ATTR("fall_capacitance", &fly::LIBPin::fall_capacitance_)
    FLY_EXPORT_ATTR("max_capacitance", &fly::LIBPin::max_capacitance_)
    FLY_EXPORT_ATTR("extra_attrs", &fly::LIBPin::extra_attrs_)
    FLY_EXPORT_ATTR("internal_powers", &fly::LIBPin::internal_powers_)
    FLY_EXPORT_ATTR("timings", &fly::LIBPin::timings_)
    FLY_EXPORT_SERIALIZE_PICKLE(fly::LIBPin);

FLY_EXPORT_CLASS(fly::LIBCell, "EXLIBCell")
    FLY_EXPORT_INIT()
    FLY_EXPORT_ATTR("name", &fly::LIBCell::name_)
    FLY_EXPORT_ATTR("area", &fly::LIBCell::area_)
    FLY_EXPORT_ATTR("is_sequence_cell", &fly::LIBCell::is_sequence_cell_)
    FLY_EXPORT_ATTR("extra_attrs", &fly::LIBCell::extra_attrs_)
    FLY_EXPORT_ATTR("pins", &fly::LIBCell::pins_)
    FLY_EXPORT_SERIALIZE_PICKLE(fly::LIBCell);

FLY_EXPORT_CLASS(fly::LIBLibrary, "EXLIBLibrary")
    FLY_EXPORT_INIT()
    FLY_EXPORT_ATTR("name", &fly::LIBLibrary::name_)
    FLY_EXPORT_ATTR("header_attrs", &fly::LIBLibrary::header_attrs_)
    FLY_EXPORT_ATTR("template_names", &fly::LIBLibrary::template_names_)
    FLY_EXPORT_ATTR("cells", &fly::LIBLibrary::cells_)
    FLY_EXPORT_ATTR("skipped_group_names", &fly::LIBLibrary::skipped_group_names_)
    FLY_EXPORT_ATTR("skipped_group_counts", &fly::LIBLibrary::skipped_group_counts_)
    FLY_EXPORT_DEF("build_cell_index", [](fly::LIBLibrary& lib) {
        lib.build_cell_index();
    })
    FLY_EXPORT_SERIALIZE_PICKLE(fly::LIBLibrary);

FLY_EXPORT_FUNCTION("lib_parse_lib_file", [](const CMString& path) {
    return new fly::LIBLibrary(fly::lib_parse_lib_file(path));
});
}
