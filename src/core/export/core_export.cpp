#include "../../export/cpp/export_macros.h"
#include "../../serialization/cpp/serialization_macros.h"
#include "../cpp/config.h"

FLY_EXPORT_MODULE_BEGIN(_fly_core)

FLY_EXPORT_CLASS_NO_INIT(m, Config,
    FLY_EXPORT_METHOD(set_int, &Config::set_int)
    FLY_EXPORT_METHOD(set_str, &Config::set_str)
    FLY_EXPORT_METHOD(get_int, &Config::get_int)
    FLY_EXPORT_METHOD(get_str, &Config::get_str)
    FLY_EXPORT_METHOD(mark_workers_launched, &Config::mark_workers_launched)
    FLY_EXPORT_METHOD(is_workers_launched, &Config::is_workers_launched)
    FLY_EXPORT_METHOD(reset, &Config::reset)
);

FLY_EXPORT_FUNCTION_REF_WITH_NAME(m, "get_config", []() { return &Config::instance(); });

FLY_EXPORT_MODULE_END()