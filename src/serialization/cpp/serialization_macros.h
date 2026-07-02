#pragma once

// =============================================================================
// Fly Serialization Macros — Library-agnostic layer
//
// Select backend by defining FLY_SERIALIZATION_BACKEND before including this header:
//   FLY_BACKEND_BITSERY  (default) — uses bitsery (header-only, versioning support)
//   FLY_BACKEND_CEREAL   (future)  — uses cereal
//
// When using bitsery backend with TrustedConfig (internal trusted data),
// FLY_SERIALIZATION_TRUSTED=1 (default) skips data validation checks.
// This allows FLY_MAX_SIZE to be a placeholder value that is never validated.
// =============================================================================

#ifndef FLY_SERIALIZATION_BACKEND
#define FLY_BACKEND_BITSERY 1
#define FLY_BACKEND_CEREAL  2
#define FLY_SERIALIZATION_BACKEND FLY_BACKEND_BITSERY
#elif !defined(FLY_BACKEND_BITSERY)
#define FLY_BACKEND_BITSERY 1
#define FLY_BACKEND_CEREAL  2
#endif

#ifndef FLY_SERIALIZATION_TRUSTED
#define FLY_SERIALIZATION_TRUSTED 1
#endif

// =============================================================================
// Backend: Bitsery
// =============================================================================
#if FLY_SERIALIZATION_BACKEND == FLY_BACKEND_BITSERY

#include <bitsery/bitsery.h>
#include <bitsery/adapter/buffer.h>
#include <bitsery/adapter/stream.h>
#include <bitsery/traits/vector.h>
#include <bitsery/traits/string.h>

#include <bitsery/ext/std_map.h>
#include <common/cpp/common_types.h>
#include <serialization/cpp/fly_buffer.h>
#include <cstdint>
#include <stdexcept>
#include <fstream>
#include <serialization/cpp/bitsery_ext/version.h>

// TrustedConfig: disables data validation for internal trusted data.
// When enabled, maxSize parameters on text/container are placeholder only.
struct FlyTrustedConfig {
    static constexpr bitsery::EndiannessType Endianness = bitsery::EndiannessType::LittleEndian;
    static constexpr bool CheckAdapterErrors = true;
    static constexpr bool CheckDataErrors = false;
};

// Max size placeholder — only used for bitsery container/text size parameters.
// With TrustedConfig, this value is never validated. It exists to satisfy
// bitsery's compile-time requirement for dynamic containers.
#define FLY_MAX_SIZE 0x7FFFFFFF

// Type aliases for Fly serialization
// FlySerBuf is now FlyBuffer — unified byte buffer for bitsery + Python + DataWriter
using FlySerBuf = FlyBuffer;
using FlyOutputAdapter = bitsery::OutputBufferAdapter<FlySerBuf>;
using FlyInputAdapter = bitsery::InputBufferAdapter<FlySerBuf, FlyTrustedConfig>;

// Stream adapter aliases
using FlyOutputStreamAdapter = bitsery::OutputBufferedStreamAdapter;
using FlyInputStreamAdapter = bitsery::InputStreamAdapter;

// =============================================================================
// Field macros — bitsery implementation
// These macros abstract field serialization so the backend can be swapped
// without modifying struct definitions.
// =============================================================================

// Declare serialization method — two forms:
//
// 1. Variadic (simple, all fields exist in version 1):
//   FLY_SERIALIZE(id, name, scores)
//
// 2. Full form (when version-dependent logic is needed):
//   FLY_SERIALIZE_BEGIN(2)
//       FLY_FIELD(name);
//       if (version >= 2) { FLY_FIELD(new_field); }
//   FLY_SERIALIZE_END
//
#define FLY_SERIALIZE(...) \
    FLY_SERIALIZE_BEGIN(1) \
        FLY_EACH(__VA_ARGS__) \
    FLY_SERIALIZE_END

// Apply FLY_FIELD to each argument via Boost.PP (eliminates 16 numbered macros)
#include <boost/preprocessor/variadic/to_seq.hpp>
#include <boost/preprocessor/seq/for_each.hpp>

#define FLY_FIELD_APPLY(r, data, field) FLY_FIELD(field);

#define FLY_EACH(...) \
    BOOST_PP_SEQ_FOR_EACH(FLY_FIELD_APPLY, _, BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__))

// Full form with explicit body.
// Usage:
//   FLY_SERIALIZE_BEGIN(2)
//       FLY_FIELD(s, o, name);
//       if (version >= 2) { FLY_FIELD(s, o, new_field); }
//   FLY_SERIALIZE_END
#define FLY_SERIALIZE_BEGIN(N) \
    template<typename S> \
    void serialize(S& s) { \
        s.ext(*this, fly::Version<N>{}, [](S& s, auto& o, size_t version) {

#define FLY_SERIALIZE_END \
        }); \
    } \
    void fly_serialize(std::ostream& fly_ss_os_) const { \
        FLY_ENCODE_TO_STREAM(fly_ss_os_, *this); \
    } \
    void fly_deserialize(std::istream& fly_ss_is_) { \
        using FlySelfType_ = std::decay_t<decltype(*this)>; \
        FLY_DECODE_FROM_STREAM(fly_ss_is_, FlySelfType_, *this); \
    }

// Field macros — simplified (recommended)
// These macros use sizeof-based deduction — no element size needed.
// Usage: FLY_VAL(s, o, value)  →  auto-deduces 4b for int32_t, 8b for double, etc.
//
// FLY_FIELD is the unified macro — replaces FLY_VAL/FLY_STR/FLY_VEC/FLY_MAP/FLY_OBJ.
// It auto-detects whether the field is a value/string/vector/map/object and dispatches accordingly.
//
// Examples:
//   FLY_FIELD(s, o, id);           // int32_t → value, auto-sized
//   FLY_FIELD(s, o, name);         // string → text (char-width auto-detected)
//   FLY_FIELD(s, o, scores);       // vector<int> → container(bulk)
//   FLY_FIELD(s, o, children);     // vector<Obj> → container(per-elem)
//   FLY_FIELD(s, o, tags);         // map<string,int> → StdMap(auto)
//   FLY_FIELD(s, o, grouped);      // map<string,vector<Obj>> → StdMap(auto)
//   FLY_FIELD(s, o, inner);        // SomeStruct → object(serialize)
// FLY_FIELD — unified auto-dispatch for any field type.
// s and o are from the enclosing FLY_SERIALIZE_BEGIN lambda — hardcoded here.
#define FLY_FIELD(field) \
    do { \
        auto& fly_v_ = o.field; \
        using fly_T_ = std::decay_t<decltype(fly_v_)>; \
        if constexpr (fly_ser::is_map_v<fly_T_>) { \
            s.ext(fly_v_, bitsery::ext::StdMap{FLY_MAX_SIZE}, [](auto& s, auto& key, auto& val) { \
                fly_ser::map_elem(s, key); \
                fly_ser::map_elem(s, val); \
            }); \
        } else if constexpr (fly_ser::is_vector_v<fly_T_>) { \
            fly_ser::container(s, fly_v_); \
        } else if constexpr (fly_ser::is_string_v<fly_T_>) { \
            fly_ser::text(s, fly_v_); \
        } else if constexpr (std::is_fundamental_v<fly_T_> || std::is_enum_v<fly_T_>) { \
            fly_ser::value(s, fly_v_); \
        } else { \
            s.object(fly_v_); \
        } \
    } while(0)

// =============================================================================
// Internal helpers — used inside lambdas where variables (not obj.field) are serialized
// =============================================================================
namespace fly_ser {

// --- Type traits for dispatch ---

template<typename T> struct is_map_impl : std::false_type {};
template<typename... A> struct is_map_impl<std::map<A...>> : std::true_type {};
template<typename... A> struct is_map_impl<std::multimap<A...>> : std::true_type {};
template<typename... A> struct is_map_impl<std::unordered_map<A...>> : std::true_type {};
template<typename... A> struct is_map_impl<std::unordered_multimap<A...>> : std::true_type {};

template<typename T>
constexpr bool is_map_v = is_map_impl<std::decay_t<T>>::value;

template<typename T> struct is_vector_impl : std::false_type {};
template<typename... A> struct is_vector_impl<std::vector<A...>> : std::true_type {};

template<typename T>
constexpr bool is_vector_v = is_vector_impl<std::decay_t<T>>::value;

template<typename T>
constexpr bool is_string_v = std::is_same_v<std::decay_t<T>, std::string>;

// --- Value helpers ---

template<typename S, typename T>
void value(S& s, T& v) {
    if constexpr (sizeof(T) == 1) s.value1b(v);
    else if constexpr (sizeof(T) == 2) s.value2b(v);
    else if constexpr (sizeof(T) == 4) s.value4b(v);
    else if constexpr (sizeof(T) == 8) s.value8b(v);
}

template<typename S, typename T>
void text(S& s, T& str) {
    s.text1b(str, FLY_MAX_SIZE);
}

template<typename S, typename T>
void object(S& s, T& obj) {
    s.object(obj);
}

template<typename S, typename T>
void container(S& s, T& c) {
    using E = typename T::value_type;
    if constexpr (std::is_fundamental_v<E>) {
        if constexpr (sizeof(E) == 1) s.container1b(c, FLY_MAX_SIZE);
        else if constexpr (sizeof(E) == 2) s.container2b(c, FLY_MAX_SIZE);
        else if constexpr (sizeof(E) == 4) s.container4b(c, FLY_MAX_SIZE);
        else if constexpr (sizeof(E) == 8) s.container8b(c, FLY_MAX_SIZE);
    } else if constexpr (is_string_v<E>) {
        s.container(c, FLY_MAX_SIZE, [](S& s, E& e) { text(s, e); });
    } else {
        s.container(c, FLY_MAX_SIZE, [](S& s, E& e) { s.object(e); });
    }
}

// --- Map element dispatch (for key/val inside StdMap lambdas) ---

template<typename S, typename T>
void map_elem(S& s, T& v) {
    using fly_T_ = std::decay_t<T>;
    if constexpr (is_map_v<fly_T_>) {
        s.ext(v, bitsery::ext::StdMap{FLY_MAX_SIZE}, [](auto& s, auto& key, auto& val) {
            map_elem(s, key);
            map_elem(s, val);
        });
    } else if constexpr (std::is_fundamental_v<fly_T_>) {
        value(s, v);
    } else if constexpr (is_string_v<fly_T_>) {
        text(s, v);
    } else if constexpr (is_vector_v<fly_T_>) {
        container(s, v);
    } else {
        object(s, v);
    }
}

} // namespace fly_ser

// =============================================================================
// Encode/Decode macros — bitsery implementation
// =============================================================================

// FLY_ENCODE: Serialize msg to output (CMString)
#define FLY_ENCODE(msg, output) \
    do { \
        FlySerBuf fly_enc_buf_; \
        auto fly_enc_size_ = bitsery::quickSerialization<FlyOutputAdapter>(fly_enc_buf_, msg); \
        fly_enc_buf_.resize(fly_enc_size_); \
        output = fly_enc_buf_.release(); \
    } while(0)

// FLY_DECODE: Deserialize input (CMString) to msg of type msg_type
#define FLY_DECODE(input, msg_type, output) \
    do { \
        FlySerBuf fly_dec_buf_; \
        fly_dec_buf_.take(CMString(input)); \
        msg_type fly_dec_msg_; \
        auto fly_dec_result_ = bitsery::quickDeserialization<FlyInputAdapter>( \
            {fly_dec_buf_.begin(), static_cast<size_t>(input.size())}, fly_dec_msg_); \
        if (fly_dec_result_.first != bitsery::ReaderError::NoError || !fly_dec_result_.second) { \
            throw std::runtime_error("FLY_DECODE: deserialization failed"); \
        } \
        output = std::move(fly_dec_msg_); \
    } while(0)

// FLY_ENCODE_TO_BUFFER: Serialize msg to output (FlyBuffer)
#define FLY_ENCODE_TO_BUFFER(msg, output) \
    do { \
        auto fly_enc_size_ = bitsery::quickSerialization<FlyOutputAdapter>(output, msg); \
        output.resize(fly_enc_size_); \
    } while(0)

// FLY_DECODE_FROM_BUFFER: Deserialize input (FlyBuffer) to msg of type msg_type
#define FLY_DECODE_FROM_BUFFER(input, msg_type, output) \
    do { \
        msg_type fly_dec_msg_; \
        auto fly_dec_result_ = bitsery::quickDeserialization<FlyInputAdapter>( \
            {input.begin(), input.size()}, fly_dec_msg_); \
        if (fly_dec_result_.first != bitsery::ReaderError::NoError || !fly_dec_result_.second) { \
            throw std::runtime_error("FLY_DECODE_FROM_BUFFER: deserialization failed"); \
        } \
        output = std::move(fly_dec_msg_); \
    } while(0)

// FLY_DECODE_FROM_STREAM: Stream-deserialize from std::istream (no intermediate buffer).
// Pair with DecompressingStreamBuf for streaming decompress + deserialize pipeline:
//   DecompressingStreamBuf dsbuf(data, size);
//   std::istream is(&dsbuf);
//   FLY_DECODE_FROM_STREAM(is, MyType, obj);
#define FLY_DECODE_FROM_STREAM(istream_ref, msg_type, output) \
    do { \
        FlyInputStreamAdapter fly_is_adapter_(istream_ref); \
        msg_type fly_dec_msg_; \
        auto fly_dec_result_ = bitsery::quickDeserialization(std::move(fly_is_adapter_), fly_dec_msg_); \
        if (fly_dec_result_.first != bitsery::ReaderError::NoError || !fly_dec_result_.second) { \
            throw std::runtime_error("FLY_DECODE_FROM_STREAM: deserialization failed"); \
        } \
        output = std::move(fly_dec_msg_); \
    } while(0)

// FLY_ENCODE_TO_STREAM: Stream-serialize to std::ostream (no intermediate buffer).
// Pair with CompressingStreamBuf for streaming serialize + compress pipeline:
//   CompressingStreamBuf csbuf(dest_stream, compressor, chunk_size);
//   std::ostream os(&csbuf);
//   FLY_ENCODE_TO_STREAM(os, obj);
#define FLY_ENCODE_TO_STREAM(ostream_ref, msg) \
    do { \
        FlyOutputStreamAdapter fly_os_adapter_(ostream_ref); \
        bitsery::Serializer<FlyOutputStreamAdapter> fly_ser_(std::move(fly_os_adapter_)); \
        fly_ser_.object(msg); \
        fly_ser_.adapter().flush(); \
    } while(0)

// =============================================================================
// Backend: Cereal (future implementation)
// =============================================================================
#elif FLY_SERIALIZATION_BACKEND == FLY_BACKEND_CEREAL
#error "Cereal backend not yet implemented. Use FLY_BACKEND_BITSERY."
#else
#error "Unknown serialization backend. Define FLY_SERIALIZATION_BACKEND as FLY_BACKEND_BITSERY or FLY_BACKEND_CEREAL."
#endif

namespace bitsery { namespace traits {

template<>
struct ContainerTraits<FlyBuffer> {
    using TValue = char;
    static constexpr bool isResizable = true;
    static constexpr bool isContiguous = true;
    static size_t size(const FlyBuffer& buf) { return buf.size(); }
    static void resize(FlyBuffer& buf, size_t n) { buf.resize(n); }
};

template<>
struct BufferAdapterTraits<FlyBuffer> {
    using TIterator = FlyBuffer::iterator;
    using TConstIterator = FlyBuffer::const_iterator;
    using TValue = char;
    static void increaseBufferSize(FlyBuffer& buf, size_t, size_t minSize) {
        buf.resize(minSize);
    }
};

}}