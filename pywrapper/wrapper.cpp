#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "utils/array.h"
#include "compressor/serf_xor_compressor.h"
#include "decompressor/serf_xor_decompressor.h"

namespace py = pybind11;

PYBIND11_MODULE(pyserf, m) {
  py::class_<Array<unsigned char>>(m, "ArrayOfBytes")
      .def(py::init<std::vector<unsigned char>>(), py::arg("vec"))
      .def(py::pickle(
          [](const Array<unsigned char>& self) {
            return py::make_tuple(std::vector(self.begin(), self.end()));
          },
          [](py::tuple t) {
            return new Array(t[0].cast<std::vector<unsigned char>>());
          }));

  py::class_<SerfXORCompressor>(m, "PySerfXORCompressor")
      .def(py::init<int, double, long>(), py::arg("window_size"),
           py::arg("max_diff"), py::arg("adjust"))
      .def("add_value", &SerfXORCompressor::AddValue)
      .def("close", &SerfXORCompressor::Close)
      .def("get", &SerfXORCompressor::compressed_bytes,
           py::return_value_policy::reference);

  py::class_<SerfXORDecompressor>(m, "PySerfXORDecompressor")
      .def(py::init<long>(), py::arg("adjust"))
      .def("decompress", &SerfXORDecompressor::Decompress);
}
