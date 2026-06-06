#pragma once

#include <common/cpp/common_types.h>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <memory>
#include <fmt/format.h>
#include <boost/preprocessor/variadic/to_seq.hpp>
#include <boost/preprocessor/seq/for_each.hpp>
#include <boost/preprocessor/tuple/elem.hpp>

namespace fly {

enum class LogLevel : uint8_t {
    DEBUG = 0,
    INFO = 1,
    WARN = 2,
    ERROR = 3,
};

class Logger {
public:
    static Logger& instance();
    static void init(const CMString& base_dir, uint64_t worker_id = 0);
    static CMString resolve_log_dir(const CMString& base_dir);
    static void shutdown();

    void vlog(LogLevel level, fmt::string_view fmt, fmt::format_args args);

    void set_level(LogLevel level);
    void flush();

private:
    Logger();
    explicit Logger(const CMString& filename, bool dual_output = false);
    void log(LogLevel level, const CMString& msg);
    CMString level_str(LogLevel level) const;
    CMString timestamp() const;
    static void _update_latest_symlink(const CMString& target_dir, const CMString& base_dir);
    static CMString _ensure_trailing_sep(const CMString& path);

    static Logger* instance_;
    CMString filename_;
    std::ofstream file_;
    std::mutex mutex_;
    LogLevel level_;
    bool dual_output_;  // true = write to both file and stderr (master mode)
};

template <typename... T>
inline void log_write(LogLevel level, fmt::format_string<T...> fmt, const T&... args) {
    fly::Logger::instance().vlog(level, fmt, fmt::make_format_args(args...));
}

}  // namespace fly

// --- Logging macros ---

#define DBG(fmt, ...)   fly::log_write(fly::LogLevel::DEBUG, FMT_STRING(fmt), ##__VA_ARGS__)
#define INFO(fmt, ...)  fly::log_write(fly::LogLevel::INFO,  FMT_STRING(fmt), ##__VA_ARGS__)
#define WARN(fmt, ...)  fly::log_write(fly::LogLevel::WARN,  FMT_STRING(fmt), ##__VA_ARGS__)
#define ERR(fmt, ...)   fly::log_write(fly::LogLevel::ERROR, FMT_STRING(fmt), ##__VA_ARGS__)

// --- CM_FORMAT_CLASS: custom struct formatting ---
// Usage (must be at global scope):
//   CM_FORMAT_CLASS(MyStruct, "a={}, b={}", obj.a, obj.b);

#define CM_FORMAT_CLASS(Type, Fmt, ...) \
    template <> struct fmt::formatter<Type> { \
        constexpr auto parse(fmt::format_parse_context& ctx) { return ctx.begin(); } \
        auto format(const Type& v, fmt::format_context& ctx) const { \
            return fmt::format_to(ctx.out(), FMT_STRING(Fmt), __VA_ARGS__); \
        } \
    }; \
    static_assert(true, "")

// --- CM_FORMAT_ENUM: auto-stringify enum values ---
// Usage: CM_FORMAT_ENUM(fly::MyEnum, VAL_A, VAL_B, VAL_C);

#define CM_ENUM_CASE_APPLY(r, Type, Value) case Type::Value: return fmt::format_to(ctx.out(), #Value);

#define CM_FORMAT_ENUM(Type, ...) \
    template <> struct fmt::formatter<Type> { \
        constexpr auto parse(fmt::format_parse_context& ctx) { return ctx.begin(); } \
        auto format(Type v, fmt::format_context& ctx) const { \
            switch (v) { \
                BOOST_PP_SEQ_FOR_EACH(CM_ENUM_CASE_APPLY, Type, BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__)) \
                default: return fmt::format_to(ctx.out(), "({})", static_cast<int>(v)); \
            } \
        } \
    };

// --- CM_FORMAT_ENUM_EX: custom string per enum value ---
// Usage: CM_FORMAT_ENUM_EX(fly::MyEnum, (VAL_A, "a"), (VAL_B, "b"));

#define CM_ENUM_PAIR_APPLY(r, Type, Pair) \
    case Type::BOOST_PP_TUPLE_ELEM(2, 0, Pair): \
        return fmt::format_to(ctx.out(), BOOST_PP_TUPLE_ELEM(2, 1, Pair));

#define CM_FORMAT_ENUM_EX(Type, ...) \
    template <> struct fmt::formatter<Type> { \
        constexpr auto parse(fmt::format_parse_context& ctx) { return ctx.begin(); } \
        auto format(Type v, fmt::format_context& ctx) const { \
            switch (v) { \
                BOOST_PP_SEQ_FOR_EACH(CM_ENUM_PAIR_APPLY, Type, BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__)) \
                default: return fmt::format_to(ctx.out(), "({})", static_cast<int>(v)); \
            } \
        } \
    };
