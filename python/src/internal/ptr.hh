#pragma once

#include <Python.h>
#include <memory>

namespace nix::python {

struct PyObjectDeleter
{
    void operator()(PyObject * const obj)
    {
        Py_DECREF(obj);
    }
};

typedef std::unique_ptr<PyObject, PyObjectDeleter> PyObjPtr;
} // namespace nix::python
