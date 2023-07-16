#pragma once

#include <Python.h>

namespace nix::python {

PyObject * eval(PyObject * self, PyObject * args, PyObject * kwdict);
}
