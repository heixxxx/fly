#pragma once

#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>
#include <nanobind/stl/map.h>
#include <nanobind/stl/shared_ptr.h>
#include <common/serialization/cpp/serialization_macros.h>
#include <storage/cpp/decompressing_streambuf.h>
#include <common/serialization/cpp/object_header.h>
#include <container/cpp/container_aliases.h>
#include <memory>
#include <string>
#include <cstring>
#include <istream>

class Database;

namespace fly_export = nanobind;

// NB_MODULE 固定定义变量 m，以下宏直接使用 m，无需重复传入
#define FLY_EXPORT_MODULE(module_name) NB_MODULE(module_name, m)

#define FLY_EXPORT_CLASS(class_type, export_name) \
    fly_export::class_<class_type>(m, export_name)

// Init — separate from class macros, user chooses whether and how to add init
#define FLY_EXPORT_INIT(...) .def(fly_export::init<__VA_ARGS__>())

// Lambda .def() — wraps lambda/non-trivial method bindings
#define FLY_EXPORT_DEF(export_name, ...) .def(export_name, __VA_ARGS__)

// All export macros below require explicit Python export name (no auto-stringify from C++ names)

#define FLY_EXPORT_ATTR(export_name, member) .def_rw(export_name, member)

#define FLY_EXPORT_READONLY_ATTR(export_name, member) .def_ro(export_name, member)

#define FLY_EXPORT_METHOD(export_name, func) .def(export_name, func)

#define FLY_EXPORT_READONLY_PROPERTY(export_name, getter) .def_prop_ro(export_name, getter)

#define FLY_EXPORT_FUNCTION(export_name, func) m.def(export_name, func)

#define FLY_EXPORT_ENUM(enum_type, export_name) fly_export::enum_<enum_type>(m, export_name)

#define FLY_EXPORT_ENUM_VALUE(export_name, ...) .value(export_name, __VA_ARGS__)

// pickle 绑定（buffer/bytes 两形态）——不依赖 Database，底层模块的
// export 用此宏即可：write_object 经 pickle 协议（__getstate__/__setstate__）
// 等价落库。
#define FLY_EXPORT_SERIALIZE_PICKLE(Cls) \
    .def("__getstate_buffer__", [](const Cls& obj) -> CMSharedPtr<FlyBuffer> { \
        auto buf = CMMakeShared<FlyBuffer>(); \
        auto& fly_buf_ref_ = *buf; \
        FLY_ENCODE_TO_BUFFER(obj, fly_buf_ref_); \
        return buf; \
    }) \
    .def("__setstate_from_buffer__", [](Cls& obj, const CMSharedPtr<FlyBuffer>& buf) { \
        ::new (&obj) Cls(); \
        /* FLY_DECODE 宏对损坏数据 throw std::runtime_error——此处是 Python pickle */ \
        /* 协议入口，局部转 value_error（ValueError，损坏 pickle 的 Python 惯例 */ \
        /* 异常类型 + 明确消息），不裸穿透 binding。 */ \
        try { \
            if (buf && buf->size() >= sizeof(uint32_t)) { \
                uint32_t fly_magic_; \
                std::memcpy(&fly_magic_, buf->data(), sizeof(uint32_t)); \
                if (fly_magic_ == FLY_OBJECT_MAGIC) { \
                    DecompressingStreamBuf fly_dsbuf_(buf->data(), buf->size()); \
                    std::istream fly_is_(&fly_dsbuf_); \
                    FLY_DECODE_FROM_STREAM(fly_is_, Cls, obj); \
                    return; \
                } \
            } \
            std::string data(buf ? buf->data() : "", buf ? buf->size() : 0); \
            FLY_DECODE(data, Cls, obj); \
        } catch (const std::exception& e) { \
            throw fly_export::value_error(e.what()); \
        } \
    }) \
    .def("__getstate__", [](const Cls& obj) -> fly_export::bytes { \
        auto buf = CMMakeShared<FlyBuffer>(); \
        auto& fly_buf_ref_ = *buf; \
        FLY_ENCODE_TO_BUFFER(obj, fly_buf_ref_); \
        return fly_export::bytes(buf->data(), buf->size()); \
    }) \
    .def("__setstate__", [](Cls& obj, fly_export::bytes state) { \
        ::new (&obj) Cls(); \
        const char* data = state.c_str(); \
        size_t size = state.size(); \
        /* 同 __setstate_from_buffer__：损坏数据局部转 value_error，不裸穿透。 */ \
        try { \
            if (size >= sizeof(uint32_t)) { \
                uint32_t fly_magic_; \
                std::memcpy(&fly_magic_, data, sizeof(uint32_t)); \
                if (fly_magic_ == FLY_OBJECT_MAGIC) { \
                    DecompressingStreamBuf fly_dsbuf_(data, size); \
                    std::istream fly_is_(&fly_dsbuf_); \
                    FLY_DECODE_FROM_STREAM(fly_is_, Cls, obj); \
                    return; \
                } \
            } \
            std::string s(data, size); \
            FLY_DECODE(s, Cls, obj); \
        } catch (const std::exception& e) { \
            throw fly_export::value_error(e.what()); \
        } \
    }) \
    .def_prop_ro("is_cpp", [](const Cls&) { return true; })

// 全量形态：pickle + db 直读写。_write_to_db/_read_from_db 需要
// 完整 Database 类型（调用方 export 需 include storage/cpp/database.h）。
#define FLY_EXPORT_SERIALIZE(Cls) \
    FLY_EXPORT_SERIALIZE_PICKLE(Cls) \
    .def("_write_to_db", [](const Cls& obj, Database& db, const CMString& name, \
                             const CMString& py_name, bool backup) -> int { \
        return static_cast<int>(db.write_object<Cls>(name, obj, py_name, backup)); \
    }) \
    .def_static("_read_from_db", [](Database& db, const CMString& name, const CMString& cache = "low") -> CMSharedPtr<Cls> { \
        return db.read_object<Cls>(name, cache); \
    })
