#pragma once

#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>
#include <nanobind/stl/map.h>
#include <nanobind/stl/shared_ptr.h>
#include <serialization/cpp/serialization_macros.h>
#include <storage/cpp/decompressing_streambuf.h>
#include <serialization/cpp/object_header.h>
#include <common/cpp/common_types.h>
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

#define FLY_EXPORT_STATIC_METHOD(export_name, func) .def_static(export_name, func)

#define FLY_EXPORT_PROPERTY(export_name, getter, setter) .def_prop_rw(export_name, getter, setter)

#define FLY_EXPORT_READONLY_PROPERTY(export_name, getter) .def_prop_ro(export_name, getter)

#define FLY_EXPORT_FUNCTION(export_name, func) m.def(export_name, func)

#define FLY_EXPORT_ENUM(enum_type, export_name) fly_export::enum_<enum_type>(m, export_name)

#define FLY_EXPORT_ENUM_VALUE(export_name, ...) .value(export_name, __VA_ARGS__)

#define FLY_EXPORT_SERIALIZE(Cls) \
    .def("__getstate__", [](const Cls& obj) -> fly_export::bytes { \
        std::string serialized; \
        FLY_ENCODE(obj, serialized); \
        return fly_export::bytes(serialized.data(), serialized.size()); \
    }) \
    .def("__getstate_buffer__", [](const Cls& obj) -> CMSharedPtr<FlyBuffer> { \
        auto buf = CMMakeShared<FlyBuffer>(); \
        auto& fly_buf_ref_ = *buf; \
        FLY_ENCODE_TO_BYTES(obj, fly_buf_ref_); \
        return buf; \
    }) \
    .def("__setstate__", [](Cls& obj, fly_export::bytes b) { \
        ::new (&obj) Cls(); \
        if (b.size() >= sizeof(uint32_t)) { \
            uint32_t fly_magic_; \
            std::memcpy(&fly_magic_, b.c_str(), sizeof(uint32_t)); \
            if (fly_magic_ == FLY_OBJECT_MAGIC) { \
                DecompressingStreamBuf fly_dsbuf_(b.c_str(), b.size()); \
                std::istream fly_is_(&fly_dsbuf_); \
                FLY_DECODE_FROM_STREAM(fly_is_, Cls, obj); \
                return; \
            } \
        } \
        std::string data(b.c_str(), b.size()); \
        FLY_DECODE(data, Cls, obj); \
    }) \
    .def("_write_to_db", [](const Cls& obj, Database& db, const CMString& name, \
                             const CMString& py_name, bool backup) -> CMString { \
        return db.write_object<Cls>(name, obj, py_name, backup); \
    }) \
    .def_static("_read_from_db", [](Database& db, const CMString& name) -> CMSharedPtr<Cls> { \
        return db.read_object<Cls>(name); \
    }) \
    .def_prop_ro("is_cpp", [](const Cls&) { return true; })
