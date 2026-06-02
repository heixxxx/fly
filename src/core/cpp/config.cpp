#include "config.h"
#include <cstdio>

Config& Config::instance() {
    static Config config;
    return config;
}

Config::Config() {
    int_values_ = INT_DEFAULTS;
    str_values_ = STR_DEFAULTS;
}

void Config::set_int(const CMString& key, int64_t value) {
    if (workers_launched_) {
        throw std::runtime_error("Config must be set before workers are launched");
    }
    int_values_[key] = value;
}

void Config::set_str(const CMString& key, const CMString& value) {
    if (workers_launched_) {
        throw std::runtime_error("Config must be set before workers are launched");
    }
    str_values_[key] = value;
}

int64_t Config::get_int(const CMString& key) const {
    auto it = int_values_.find(key);
    auto default_it = INT_DEFAULTS.find(key);
    if (it != int_values_.end()) return it->second;
    if (default_it != INT_DEFAULTS.end()) return default_it->second;
    fprintf(stderr, "[ERR] Config::get_int: unknown key '%s'\n", key.c_str());
    return INVALID_INT;
}

const CMString& Config::get_str(const CMString& key) const {
    auto it = str_values_.find(key);
    auto default_it = STR_DEFAULTS.find(key);
    if (it != str_values_.end()) return it->second;
    if (default_it != STR_DEFAULTS.end()) return default_it->second;
    static const CMString empty = "";
    return empty;
}

void Config::mark_workers_launched() {
    workers_launched_ = true;
}

bool Config::is_workers_launched() const {
    return workers_launched_;
}

void Config::reset() {
    int_values_ = INT_DEFAULTS;
    str_values_ = STR_DEFAULTS;
    workers_launched_ = false;
}

const CMMap<CMString, int64_t> Config::INT_DEFAULTS = {
    {"worker_mode", 0},
    {"worker_id", 0},
    {"master_port", 8000},
    {"heartbeat_timeout", 120},
    {"heartbeat_interval", 5},
    {"backup_threshold", 100},
    {"aggregation_threshold", 1048576},
    {"large_file_threshold_kb", 65536},  // 64MB in KB (user-configurable)
    {"block_size", 134217728},
    {"track_writes", 0},
    {"data_server_threads", 1},
    {"compression_level", 0},
    {"serialize_chunk_size", 4194304},
    {"dependency_update_mode", 0},
    {"fail_unscheduleable_tasks", 1},
    {"interactive", 0},
    {"cli_master_port", 0},
    {"read_cache_size", 1073741824},
    {"temp_store_size", 536870912},
};

const CMMap<CMString, CMString> Config::STR_DEFAULTS = {
    {"transport_type", "tcp"},
    {"compression_type", "lz4"},
    {"data_server_host", "127.0.0.1"},
    {"master_host", "127.0.0.1"},
    {"log_dir", "fly_log"},
    {"script_path", ""},
};
