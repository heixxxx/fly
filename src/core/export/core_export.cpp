#include <export/cpp/export_macros.h>
#include <serialization/cpp/serialization_macros.h>
#include <core/cpp/config.h>

FLY_EXPORT_MODULE(_fly_core) {

FLY_EXPORT_CLASS(Config, "EXCoreConfig")
    FLY_EXPORT_METHOD("set_int", &Config::set_int)
    FLY_EXPORT_METHOD("set_str", &Config::set_str)
    FLY_EXPORT_METHOD("get_int", &Config::get_int)
    FLY_EXPORT_METHOD("get_str", &Config::get_str)
    FLY_EXPORT_METHOD("mark_workers_launched", &Config::mark_workers_launched)
    FLY_EXPORT_METHOD("is_workers_launched", &Config::is_workers_launched)
    FLY_EXPORT_METHOD("reset", &Config::reset);

FLY_EXPORT_FUNCTION_REF("ex_core_get_config", []() { return &Config::instance(); });

}
