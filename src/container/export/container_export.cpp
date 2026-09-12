// _fly_common.so — common 模块 Python 绑定入口。
// 导出框架级公共查找表（CMLookupTable/CMLookupTableTemplate）：
// FLY_EXPORT_SERIALIZE_PICKLE 提供 pickle（__getstate__/__setstate__），
// write_object 经 pickle 协议等价落库——common 是最底层模块，不依赖
// storage，故不用含 db 直读写的 FLY_EXPORT_SERIALIZE 全量形态。
#include <export/cpp/export_macros.h>
#include <container/cpp/lookup_table.h>
#include <nanobind/stl/vector.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/optional.h>

FLY_EXPORT_MODULE(_fly_container) {

FLY_EXPORT_CLASS(fly::CMLookupTableTemplate, "EXCMLookupTableTemplate")
    FLY_EXPORT_INIT()
    FLY_EXPORT_ATTR("variable_names", &fly::CMLookupTableTemplate::variable_names_)
    FLY_EXPORT_ATTR("index_sets", &fly::CMLookupTableTemplate::index_sets_)
    FLY_EXPORT_DEF("dim", [](const fly::CMLookupTableTemplate& t) -> int {
        return static_cast<int>(t.dim());
    })
    FLY_EXPORT_SERIALIZE_PICKLE(fly::CMLookupTableTemplate);

FLY_EXPORT_CLASS(fly::CMLookupTable, "EXCMLookupTable")
    FLY_EXPORT_INIT()
    FLY_EXPORT_ATTR("name", &fly::CMLookupTable::name_)
    FLY_EXPORT_ATTR("dim", &fly::CMLookupTable::dim_)
    FLY_EXPORT_ATTR("template_name", &fly::CMLookupTable::template_name_)
    FLY_EXPORT_ATTR("variable_names", &fly::CMLookupTable::variable_names_)
    FLY_EXPORT_ATTR("index_sets", &fly::CMLookupTable::index_sets_)
    FLY_EXPORT_ATTR("values", &fly::CMLookupTable::values_)
    FLY_EXPORT_DEF("resolve_template", &fly::CMLookupTable::resolve_template)
    FLY_EXPORT_DEF("is_ready", &fly::CMLookupTable::is_ready)
    // 插值失败（表未就绪 / coords 数量错）返回 None 而非异常——Python 侧非法
    // 调用语义 = None（2026-09-12 裁定，lookup_table 错误处理 bool 化零 fatal）。
    FLY_EXPORT_DEF("interpolate", [](const fly::CMLookupTable& t,
                                     const std::vector<double>& coords)
                                     -> std::optional<double> {
        double result = 0.0;
        if (!t.interpolate(coords, result)) {
            return std::nullopt;
        }
        return result;
    })
    FLY_EXPORT_SERIALIZE_PICKLE(fly::CMLookupTable);
}
