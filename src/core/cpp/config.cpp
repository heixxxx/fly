#include "config.h"
#include <cstdio>
#include <fstream>
#include <sstream>

CMSharedPtr<Config> Config::instance() {
    static CMSharedPtr<Config> inst = CMMakeShared<Config>();
    return inst;
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

void Config::save_to_file(const CMString& path) const {
    std::ofstream ofs(path.c_str(), std::ios::trunc);
    for (const auto& [k, v] : int_values_) {
        ofs << "i " << k << " " << v << "\n";
    }
    for (const auto& [k, v] : str_values_) {
        ofs << "s " << k << " " << v << "\n";
    }
}

void Config::load_from_file(const CMString& path) {
    std::ifstream ifs(path.c_str());
    if (!ifs.is_open()) return;
    CMString line;
    while (std::getline(ifs, line)) {
        if (line.size() < 3) continue;
        char type = line[0];
        CMString rest = line.substr(2);
        auto sp = rest.find(' ');
        if (sp == CMString::npos) continue;
        CMString key = rest.substr(0, sp);
        CMString val = rest.substr(sp + 1);
        if (type == 'i') {
            try { int_values_[key] = std::stoll(val); } catch (...) {}
        } else if (type == 's') {
            str_values_[key] = val;
        }
    }
}

const CMUnorderedMap<CMString, int64_t> Config::INT_DEFAULTS = {
    {"heartbeat_timeout", 120},
    {"heartbeat_interval", 5},
    {"backup_threshold", 100},
    {"auto_backup_enabled", 0},       // 0=disabled, 1=enabled
    {"backup_replicas", 2},           // target number of backup copies (including original)
    {"backup_decay_interval", 300},   // decay check interval in seconds, 0=no decay
    {"backup_decay_factor", 50},      // decay factor percentage (read_count *= factor/100)
    {"aggregation_threshold", 1048576},
    {"large_file_threshold_kb", 65536},  // 64MB in KB (user-configurable)
    {"block_size", 134217728},
    {"track_writes", 0},
    {"data_server_threads", 4},
    {"compression_level", 0},
    {"serialize_chunk_size", 4194304},
    {"dependency_update_mode", 0},
    {"fail_unscheduleable_tasks", 1},
    {"read_cache_size", 1073741824},
    {"temp_store_size", 2147483648},
    {"data_client_pool_size", 4},
};

const CMUnorderedMap<CMString, CMString> Config::STR_DEFAULTS = {
    {"transport_type", "tcp"},
    {"compression_type", "lz4"},
    {"log_dir", "fly_log"},
};
