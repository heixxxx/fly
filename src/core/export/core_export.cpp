#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include "../cpp/config.h"

namespace nb = nanobind;

NB_MODULE(_core, m) {
    nb::class_<Config>(m, "Config")
        .def("set_int", &Config::set_int)
        .def("set_str", &Config::set_str)
        .def("get_int", &Config::get_int)
        .def("get_str", &Config::get_str, nb::rv_policy::reference)
        .def("mark_workers_launched", &Config::mark_workers_launched)
        .def("is_workers_launched", &Config::is_workers_launched)
        .def("reset", &Config::reset);
    
    m.def("get_config", []() { return &Config::instance(); }, nb::rv_policy::reference);
}