// fly/src/core/cpp/config.h
// Config singleton for Fly distributed task framework
// Thread-safe configuration management with worker launch protection

#pragma once

#include <map>
#include <string>
#include <cstdint>
#include <stdexcept>

class Config {
public:
    // Singleton access
    static Config& instance();

    // Setters (throws if workers already launched)
    void set_int(const std::string& key, int64_t value);
    void set_str(const std::string& key, const std::string& value);

    // Getters (returns default if not set, throws for unknown key)
    int64_t get_int(const std::string& key) const;
    const std::string& get_str(const std::string& key) const;

    // Worker launch state management
    void mark_workers_launched();
    bool is_workers_launched() const;

    // Reset for testing
    void reset();

private:
    Config();
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;

    std::map<std::string, int64_t> int_values_;
    std::map<std::string, std::string> str_values_;
    bool workers_launched_ = false;

    // Default values
    static const std::map<std::string, int64_t> INT_DEFAULTS;
    static const std::map<std::string, std::string> STR_DEFAULTS;
};