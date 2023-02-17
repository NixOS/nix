#include <Python.h>

#include "internal/eval.hh"
#include "internal/ptr.hh"

#include <config.h>

#include <eval.hh>
#include <globals.hh>
#include <shared.hh>

namespace nix::python {

#define _public_ __attribute__((visibility("default")))

PyObject * NixError = nullptr;

static PyMethodDef NixMethods[] = {
    {"eval", (PyCFunction) eval, METH_VARARGS | METH_KEYWORDS, "Eval nix expression"}, {NULL, NULL, 0, NULL}};

static struct PyModuleDef nixmodule = {
    PyModuleDef_HEAD_INIT, "nix", "Nix expression bindings",
    -1, /* size of per-interpreter state of the module,
           or -1 if the module keeps state in global variables. */
    NixMethods};

extern "C" _public_ PyObject * PyInit_nix(void)
{
    // By default, Nix sets the build-hook to be "$(readlink /proc/self/exe) __build-remote", expecting the current
    // binary to be Nix itself. But when we call the Nix library from Python this isn't the case, the current binary is
    // Python then So we need to change this default, pointing it to the Nix binary instead
    nix::settings.buildHook = nix::settings.nixBinDir + "/nix __build-remote";
    // And by setting buildHook before calling initNix, we can override the defaults without overriding the
    // user-provided options from the config files
    nix::initNix();
    nix::initGC();

    PyObjPtr m(PyModule_Create(&nixmodule));

    if (!m) {
        return nullptr;
    }

    NixError = PyErr_NewExceptionWithDoc(
        "nix.NixError",                             /* char *name */
        "Base exception class for the nix module.", /* char *doc */
        NULL,                                       /* PyObject *base */
        NULL                                        /* PyObject *dict */
    );

    if (!NixError) {
        return nullptr;
    }

    if (PyModule_AddObject(m.get(), "NixError", NixError) == -1) {
        return nullptr;
    }

    return m.release();
}
} // namespace nix::python
