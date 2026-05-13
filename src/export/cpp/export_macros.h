#pragma once

#include <nanobind/nanobind.h>
#include <nanobind/stl.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>
#include <nanobind/stl/map.h>
#include <nanobind/stl/shared_ptr.h>
#include <memory>
#include <string>

namespace nb = nanobind;

// FLY_EXPORT_PICKLE - add pickle support using FLY_ENCODE/FLY_DECODE
#define FLY_EXPORT_PICKLE(class_type) \
    .def(nb::pickle( \
        [](const class_type& obj) { \
            std::string serialized; \
            FLY_ENCODE(obj, serialized); \
            return nb::bytes(serialized); \
        }, \
        [](nb::bytes bytes) { \
            std::string data = bytes.c_str(); \
            class_type obj; \
            FLY_DECODE(data, class_type, obj); \
            return obj; \
        } \
    ))

// FLY_EXPORT_CLASS - export class with pickle support
#define FLY_EXPORT_CLASS(module, class_type, ...) \
    nb::class_<class_type>(module, #class_type) \
        .def(nb::init<>()) \
        __VA_ARGS__ \
        FLY_EXPORT_PICKLE(class_type)

// FLY_EXPORT_CLASS_SHARED_PTR - export class with shared_ptr holder
#define FLY_EXPORT_CLASS_SHARED_PTR(module, class_type, ...) \
    nb::class_<class_type, std::shared_ptr<class_type>>(module, #class_type) \
        .def(nb::init<>()) \
        __VA_ARGS__ \
        FLY_EXPORT_PICKLE(class_type)

// FLY_EXPORT_ATTR - export read-write attribute
#define FLY_EXPORT_ATTR(name, member) .def_rw(#name, member)

// FLY_EXPORT_METHOD - export method
#define FLY_EXPORT_METHOD(name, func) .def(#name, func)