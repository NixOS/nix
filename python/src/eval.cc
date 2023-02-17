#include "internal/eval.hh"
#include "internal/errors.hh"
#include "internal/nix-to-python.hh"
#include "internal/python-to-nix.hh"
#include <Python.h>

#include <cxxabi.h>
#include <store-api.hh>

namespace pythonnix {

const char * currentExceptionTypeName()
{
    int status;
    auto res = abi::__cxa_demangle(abi::__cxa_current_exception_type()->name(), 0, 0, &status);
    return res ? res : "(null)";
}

static PyObject * _eval(const char * expression, PyObject * vars)
{
    nix::Strings storePath;
    nix::EvalState state(storePath, nix::openStore());

    nix::Env * env = nullptr;
    auto staticEnv = pythonToNixEnv(state, vars, &env);
    if (!staticEnv) {
        return nullptr;
    }
    auto staticEnvPointer = std::make_shared<nix::StaticEnv>(*staticEnv);

    auto e = state.parseExprFromString(expression, ".", staticEnvPointer);
    nix::Value v;
    e->eval(state, *env, v);

    state.forceValueDeep(v);

    nix::PathSet context;
    return nixToPythonObject(state, v, context);
}

PyObject * eval(PyObject * self, PyObject * args, PyObject * keywds)
{
    const char * expression = nullptr;
    PyObject * vars = nullptr;

    const char * kwlist[] = {"expression", "vars", nullptr};

    if (!PyArg_ParseTupleAndKeywords(
            args, keywds, "s|O!", const_cast<char **>(kwlist), &expression, &PyDict_Type, &vars)) {
        return nullptr;
    }

    try {
        return _eval(expression, vars);
    } catch (nix::Error & e) {
        return PyErr_Format(NixError, "%s", e.what());
    } catch (...) {
        return PyErr_Format(NixError, "unexpected C++ exception: '%s'", currentExceptionTypeName());
    }
}
} // namespace pythonnix
