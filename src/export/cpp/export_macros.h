#pragma once

#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>
#include <nanobind/stl/map.h>
#include <nanobind/stl/shared_ptr.h>
#include <memory>
#include <string>

namespace fly_export = nanobind;

#define FLY_EXPORT_MODULE_BEGIN(module_name) NB_MODULE(module_name, m) {

#define FLY_EXPORT_MODULE_END() }

#define FLY_EXPORT_PICKLE(class_type) \
    .def(fly_export::pickle( \
        [](const class_type& obj) { \
            std::string serialized; \
            FLY_ENCODE(obj, serialized); \
            return fly_export::bytes(serialized); \
        }, \
        [](fly_export::bytes bytes) { \
            std::string data = bytes.c_str(); \
            class_type obj; \
            FLY_DECODE(data, class_type, obj); \
            return obj; \
        } \
    ))

#define FLY_EXPORT_PICKLE_SHARED_PTR(class_type) \
    .def(fly_export::pickle( \
        [](const std::shared_ptr<class_type>& obj) { \
            std::string serialized; \
            FLY_ENCODE(*obj, serialized); \
            return fly_export::bytes(serialized); \
        }, \
        [](fly_export::bytes bytes) { \
            std::string data = bytes.c_str(); \
            class_type obj; \
            FLY_DECODE(data, class_type, obj); \
            return std::make_shared<class_type>(std::move(obj)); \
        } \
    ))

#define FLY_EXPORT_CLASS(module_var, class_type, ...) \
    fly_export::class_<class_type>(module_var, #class_type) \
        .def(fly_export::init<>()) \
        __VA_ARGS__ \
        FLY_EXPORT_PICKLE(class_type)

#define FLY_EXPORT_CLASS_NO_INIT(module_var, class_type, ...) \
    fly_export::class_<class_type>(module_var, #class_type) \
        __VA_ARGS__

#define FLY_EXPORT_CLASS_WITH_NAME(module_var, class_type, export_name, ...) \
    fly_export::class_<class_type>(module_var, export_name) \
        .def(fly_export::init<>()) \
        __VA_ARGS__ \
        FLY_EXPORT_PICKLE(class_type)

#define FLY_EXPORT_CLASS_SHARED_PTR(module_var, class_type, ...) \
    fly_export::class_<class_type, std::shared_ptr<class_type>>(module_var, #class_type) \
        .def(fly_export::init<>()) \
        __VA_ARGS__ \
        FLY_EXPORT_PICKLE_SHARED_PTR(class_type)

#define FLY_EXPORT_CLASS_SHARED_PTR_NO_INIT(module_var, class_type, ...) \
    fly_export::class_<class_type, std::shared_ptr<class_type>>(module_var, #class_type) \
        __VA_ARGS__

#define FLY_EXPORT_CLASS_SHARED_PTR_WITH_NAME(module_var, class_type, export_name, ...) \
    fly_export::class_<class_type, std::shared_ptr<class_type>>(module_var, export_name) \
        .def(fly_export::init<>()) \
        __VA_ARGS__ \
        FLY_EXPORT_PICKLE_SHARED_PTR(class_type)

#define FLY_EXPORT_ATTR(name, member) .def_rw(#name, member)

#define FLY_EXPORT_ATTR_WITH_NAME(export_name, member) .def_rw(export_name, member)

#define FLY_EXPORT_READONLY_ATTR(name, member) .def_ro(#name, member)

#define FLY_EXPORT_READONLY_ATTR_WITH_NAME(export_name, member) .def_ro(export_name, member)

#define FLY_EXPORT_METHOD(name, func) .def(#name, func)

#define FLY_EXPORT_METHOD_WITH_NAME(export_name, func) .def(export_name, func)

#define FLY_EXPORT_STATIC_METHOD(name, func) .def_static(#name, func)

#define FLY_EXPORT_STATIC_METHOD_WITH_NAME(export_name, func) .def_static(export_name, func)

#define FLY_EXPORT_PROPERTY(name, getter, setter) .def_prop_rw(#name, getter, setter)

#define FLY_EXPORT_PROPERTY_WITH_NAME(export_name, getter, setter) .def_prop_rw(export_name, getter, setter)

#define FLY_EXPORT_READONLY_PROPERTY(name, getter) .def_prop_ro(#name, getter)

#define FLY_EXPORT_READONLY_PROPERTY_WITH_NAME(export_name, getter) .def_prop_ro(export_name, getter)

#define FLY_EXPORT_FUNCTION(module_var, name, func) module_var.def(#name, func)

#define FLY_EXPORT_FUNCTION_WITH_NAME(module_var, export_name, func) module_var.def(export_name, func)

#define FLY_EXPORT_FUNCTION_REF(module_var, name, func) module_var.def(#name, func, nanobind::rv_policy::reference)

#define FLY_EXPORT_FUNCTION_REF_WITH_NAME(module_var, export_name, func) module_var.def(export_name, func, nanobind::rv_policy::reference)

#define FLY_EXPORT_ENUM(module_var, enum_type) fly_export::enum_<enum_type>(module_var, #enum_type)

#define FLY_EXPORT_ENUM_WITH_NAME(module_var, enum_type, export_name) fly_export::enum_<enum_type>(module_var, export_name)

#define FLY_EXPORT_ENUM_VALUE(enum_type, name) .value(#name, enum_type::name)

#define FLY_EXPORT_ENUM_VALUE_WITH_NAME(enum_type, export_name, value) .value(export_name, enum_type::value)