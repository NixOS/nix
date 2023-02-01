#pragma once

#include <Python.h>

namespace pythonnix {

PyObject *eval(PyObject *self, PyObject *args, PyObject *kwdict);
}
