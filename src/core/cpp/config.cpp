#include "config.h"

Config& Config::instance() {
    static Config config;
    return config;
}

Config::Config() {
    int_values_ = INT_DEFAULTS;
    str_values_ = STR_DEFAULTS;
}

void Config::set_int(const std::string& key, int64_t value) {
    if (workers_launched_) {
        throw std::runtime_error("Config must be set before workers are launched");
    }
    int_values_[key] = value;
}

void Config::set_str(const std::string& key, const std::string& value) {
    if (workers_launched_) {
        throw std::runtime_error("Config must be set before workers are launched");
    }
    str_values_[key] = value;
}

int64_t Config::get_int(const std::string& key) const {
    auto it = int_values_.find(key);
    auto default_it = INT_DEFAULTS.find(key);
    if (it != int_values_.end()) return it->second;
    if (default_it != INT_DEFAULTS.end()) return default_it->second;
    throw std::runtime_error("Unknown config key: " + key);
}

const std::string& Config::get_str(const std::string& key) const {
    auto it = str_values_.find(key);
    auto default_it = STR_DEFAULTS.find(key);
    if (it != str_values_.end()) return it->second;
    if (default_it != STR_DEFAULTS.end()) return default_it->second;
    static const std::string empty = "";
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

const std::map<std::string, int64_t> Config::INT_DEFAULTS = {
    {"master_port", 8000},
    {"heartbeat_timeout", 120},
    {"heartbeat_interval", 5},
    {"backup_threshold", 100},
    {"aggregation_threshold", 1048576},
    {"large_file_threshold", 10485760},
    {"block_size", 134217728},
    {"track_writes", 0},
    {"data_server_threads", 1},
};

const std::map<std::string, std::string> Config::STR_DEFAULTS = {
    {"transport_type", "tcp"},
};