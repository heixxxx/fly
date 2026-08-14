#pragma once

#include <common/cpp/common_types.h>
#include <cstdint>
#include <shared_mutex>
#include <stdexcept>

#include <climits>

class Config {
public:
    Config();

    static constexpr int64_t INVALID_INT = INT64_MIN;

    static CMSharedPtr<Config> instance();

    void set_int(const CMString& key, int64_t value);
    void set_str(const CMString& key, const CMString& value);

    int64_t get_int(const CMString& key) const;
    // 返回 by value：锁内拷贝后释放，避免调用方持引用期间并发 set_str 触发 rehash 造成悬空引用
    CMString get_str(const CMString& key) const;

    void mark_workers_launched();
    bool is_workers_launched() const;

    void reset();

    void save_to_file(const CMString& path) const;
    void load_from_file(const CMString& path);

private:
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;

    CMUnorderedMap<CMString, int64_t> int_values_;
    CMUnorderedMap<CMString, CMString> str_values_;
    bool workers_launched_ = false;
    // 保护 int_values_/str_values_/workers_launched_ 的并发读写。
    // Config 是进程内共享单例，get_* 从 reactor/heartbeat/scheduler 等多线程并发读取，
    // set_* 经 FFI 暴露给 Python 可在运行时调用，故所有底层访问须持锁。leaf lock，无反向调用，无死锁风险。
    mutable std::shared_mutex mutex_;

    static const CMUnorderedMap<CMString, int64_t> INT_DEFAULTS;
    static const CMUnorderedMap<CMString, CMString> STR_DEFAULTS;
};