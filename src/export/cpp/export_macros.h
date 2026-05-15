#pragma once

#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>
#include <nanobind/stl/map.h>
#include <nanobind/stl/shared_ptr.h>
#include <memory>
#include <string>

namespace fly_export = nanobind;

// NB_MODULE 固定定义变量 m，以下宏直接使用 m，无需重复传入
#define FLY_EXPORT_MODULE(module_name) NB_MODULE(module_name, m)

#define FLY_EXPORT_CLASS(class_type, export_name) \
    fly_export::class_<class_type>(m, export_name)

#define FLY_EXPORT_CLASS_SHARED_PTR(class_type, export_name) \
    fly_export::class_<class_type, std::shared_ptr<class_type>>(m, export_name)

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

#define FLY_EXPORT_FUNCTION_REF(export_name, func) m.def(export_name, func, nanobind::rv_policy::reference)

#define FLY_EXPORT_ENUM(enum_type, export_name) fly_export::enum_<enum_type>(m, export_name)

#define FLY_EXPORT_ENUM_VALUE(export_name, ...) .value(export_name, __VA_ARGS__)

#define FLY_EXPORT_SERIALIZE(Cls) \
    .def("__getstate__", [](const Cls& obj) -> fly_export::bytes { \
        std::string serialized; \
        FLY_ENCODE(obj, serialized); \
        return fly_export::bytes(serialized.data(), serialized.size()); \
    }) \
    .def("__setstate__", [](Cls& obj, fly_export::bytes b) { \
        std::string data(b.c_str(), b.size()); \
        FLY_DECODE(data, Cls, obj); \
    }) \
    .def_prop_ro("is_cpp", [](const Cls&) { return true; })
