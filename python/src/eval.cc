#include "internal/eval.hh"
#include "internal/errors.hh"
#include "internal/nix-to-python.hh"
#include "internal/python-to-nix.hh"
#include <Python.h>

#include <cxxabi.h>
#include <store-api.hh>
#include <finally.hh>

namespace nix::python {

const char * currentExceptionTypeName()
{
    int status;
    auto res = abi::__cxa_demangle(abi::__cxa_current_exception_type()->name(), 0, 0, &status);
    return res ? res : "(null)";
}

static PyObject * _eval(const std::string & expression, PyObject * argument)
{
    nix::Strings storePath;
    nix::EvalState state(storePath, nix::openStore());

    auto nixArgument = pythonToNixValue(state, argument);
    if (!nixArgument) {
        return nullptr;
    }
    nix::Value fun;
    nix::Value v;


    // Release the GIL, so that other Python threads can be running in parallel
    // while the potentially expensive Nix evaluation happens. This is safe
    // because we don't operate on Python objects or call the Python/C API in
    // this block
    // See https://docs.python.org/3/c-api/init.html#thread-state-and-the-global-interpreter-lock
    {
        PyThreadState *_save;
        _save = PyEval_SaveThread();
        Finally reacquireGIL([&] {
            PyEval_RestoreThread(_save);
        });

        // TODO: Should the "." be something else here?
        auto e = state.parseExprFromString(expression, ".");
        state.eval(e, fun);
        // TODO: Add position
        state.callFunction(fun, *nixArgument, v, noPos);
        state.forceValueDeep(v);
    }

    nix::PathSet context;
    return nixToPythonObject(state, v, context);
}

// TODO: Rename this function to callExprString, matching the Python name
PyObject * eval(PyObject * self, PyObject * args, PyObject * keywds)
{
    PyObject * expressionObject;
    PyObject * argument = nullptr;

    const char * kwlist[] = {"expression", "arg", nullptr};

    // See https://docs.python.org/3/c-api/arg.html for the magic string
    if (!PyArg_ParseTupleAndKeywords(
            args, keywds, "UO", const_cast<char **>(kwlist), &expressionObject, &argument)) {
        return nullptr;
    }

    // This handles null bytes in expressions correctly
    Py_ssize_t expressionSize;
    auto expressionBase = PyUnicode_AsUTF8AndSize(expressionObject, &expressionSize);
    if (!expressionBase) {
        return nullptr;
    }
    std::string expression(expressionBase, expressionSize);

    try {
        return _eval(expression, argument);
    } catch (nix::ThrownError & e) {
        return PyErr_Format(ThrownNixError, "%s", e.message().c_str());
    } catch (nix::Error & e) {
        return PyErr_Format(NixError, "%s", e.what());
    } catch (...) {
        return PyErr_Format(NixError, "unexpected C++ exception: '%s'", currentExceptionTypeName());
    }
}
} // namespace nix::python
