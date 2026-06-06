#include <export/cpp/export_macros.h>
#include <serialization/cpp/serialization_macros.h>
#include <core/cpp/config.h>
#include <core/cpp/graceful_exit.h>
#include <core/cpp/process_info.h>
#include <common/cpp/error_types.h>

FLY_EXPORT_MODULE(_fly_core) {

    FLY_EXPORT_CLASS(Config, "EXCoreConfig")
    FLY_EXPORT_METHOD("set_int", &Config::set_int)
    FLY_EXPORT_METHOD("set_str", &Config::set_str)
    FLY_EXPORT_METHOD("get_int", &Config::get_int)
    FLY_EXPORT_METHOD("get_str", &Config::get_str)
    FLY_EXPORT_METHOD("mark_workers_launched", &Config::mark_workers_launched)
    FLY_EXPORT_METHOD("is_workers_launched", &Config::is_workers_launched)
    FLY_EXPORT_METHOD("reset", &Config::reset)
    FLY_EXPORT_METHOD("save_to_file", &Config::save_to_file)
    FLY_EXPORT_METHOD("load_from_file", &Config::load_from_file);

    FLY_EXPORT_FUNCTION_REF("ex_core_get_config", []() { return &Config::instance(); });

    FLY_EXPORT_CLASS(ProcessInfo, "EXProcessInfo")
    FLY_EXPORT_METHOD("worker_mode", &ProcessInfo::worker_mode)
    FLY_EXPORT_METHOD("worker_id", &ProcessInfo::worker_id)
    FLY_EXPORT_METHOD("master_host", &ProcessInfo::master_host)
    FLY_EXPORT_METHOD("master_port", &ProcessInfo::master_port)
    FLY_EXPORT_METHOD("cli_master_port", &ProcessInfo::cli_master_port)
    FLY_EXPORT_METHOD("data_server_host", &ProcessInfo::data_server_host)
    FLY_EXPORT_METHOD("script_path", &ProcessInfo::script_path)
    FLY_EXPORT_METHOD("interactive", &ProcessInfo::interactive)
    FLY_EXPORT_METHOD("worker_attributes", &ProcessInfo::worker_attributes)
    FLY_EXPORT_METHOD("hostname", &ProcessInfo::hostname)
    FLY_EXPORT_METHOD("set_hostname", &ProcessInfo::set_hostname);

    FLY_EXPORT_FUNCTION_REF("ex_core_get_process_info", []() { return &ProcessInfo::instance(); });

    FLY_EXPORT_FUNCTION("graceful_exit", [](const CMString& reason, int error_type, int exit_code) {
        fly::graceful_exit(reason, static_cast<fly::TaskErrorType>(error_type), exit_code);
    });
}
