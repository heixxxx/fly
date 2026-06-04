#pragma once

#include <common/cpp/common_types.h>
#include <cstdint>
#include <stdexcept>

#include <climits>

class Config {
public:
    static constexpr int64_t INVALID_INT = INT64_MIN;

    static Config& instance();

    void set_int(const CMString& key, int64_t value);
    void set_str(const CMString& key, const CMString& value);

    int64_t get_int(const CMString& key) const;
    const CMString& get_str(const CMString& key) const;

    void mark_workers_launched();
    bool is_workers_launched() const;

    void reset();

    void apply_sync(const CMMap<CMString, int64_t>& ints,
                    const CMMap<CMString, CMString>& strs);

    const CMMap<CMString, int64_t>& all_ints() const { return int_values_; }
    const CMMap<CMString, CMString>& all_strs() const { return str_values_; }

private:
    Config();
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;

    CMMap<CMString, int64_t> int_values_;
    CMMap<CMString, CMString> str_values_;
    bool workers_launched_ = false;

    static const CMMap<CMString, int64_t> INT_DEFAULTS;
    static const CMMap<CMString, CMString> STR_DEFAULTS;
};