#include <Python.h>

#include "internal/errors.hh"
#include "internal/nix-to-python.hh"
#include "internal/ptr.hh"

namespace nix::python {

PyObject * nixToPythonObject(nix::EvalState & state, nix::Value & v, nix::PathSet & context) {
    std::set<const void *> seen;
    return _nixToPythonObject(state, v, context, seen);
}

PyObject * _nixToPythonObject(nix::EvalState & state, nix::Value & v, nix::PathSet & context, std::set<const void *> seen)
{
    switch (v.type()) {
    case nix::nInt:
        return PyLong_FromLong(v.integer);

    case nix::nBool:
        if (v.boolean) {
            Py_RETURN_TRUE;
        } else {
            Py_RETURN_FALSE;
        }
    case nix::nString:
        copyContext(v, context);
        return PyUnicode_FromString(v.string.s);

    case nix::nPath: {
        auto p = state.copyPathToStore(context, v.path).to_string();
        return PyUnicode_FromStringAndSize(p.data(), p.length());
    }

    case nix::nNull:
        Py_RETURN_NONE;

    case nix::nAttrs: {
        if (!v.attrs->empty() && !seen.insert(v.attrs).second) {
            PyErr_Format(NixError, "Infinite recursion in data structure");
            return nullptr;
        }
        auto i = v.attrs->find(state.sOutPath);
        if (i == v.attrs->end()) {
            PyObjPtr dict(PyDict_New());
            if (!dict) {
                return (PyObject *) nullptr;
            }

            for (auto & j : *v.attrs) {
                const std::string & name = state.symbols[j.name];
                auto value = _nixToPythonObject(state, *j.value, context, seen);
                if (!value) {
                    return nullptr;
                }
                PyDict_SetItemString(dict.get(), name.c_str(), value);
            }
            return dict.release();
        } else {
            return _nixToPythonObject(state, *i->value, context, seen);
        }
    }

    case nix::nList: {
        if (v.listSize() && !seen.insert(v.listElems()).second) {
            PyErr_Format(NixError, "Infinite recursion in data structure");
            return nullptr;
        }
        PyObjPtr list(PyList_New(v.listSize()));
        if (!list) {
            return (PyObject *) nullptr;
        }

        for (unsigned int n = 0; n < v.listSize(); ++n) {
            auto value = _nixToPythonObject(state, *v.listElems()[n], context, seen);
            if (!value) {
                return nullptr;
            }
            PyList_SET_ITEM(list.get(), n, value);
        }
        return list.release();
    }

    case nix::nExternal:
        return PyUnicode_FromString("<unevaluated>");

    case nix::nThunk:
        return PyUnicode_FromString("<thunk>");

    case nix::nFunction:
        return PyUnicode_FromString("<function>");

    case nix::nFloat:
        return PyFloat_FromDouble(v.fpoint);

    default:
        PyErr_Format(NixError, "cannot convert nix type '%s' to a python object", showType(v).c_str());
        return nullptr;
    }
}
} // namespace nix::python
