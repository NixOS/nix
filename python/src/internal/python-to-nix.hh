#pragma once

#include <Python.h>
#include <eval.hh>
#include <optional>

namespace nix::python {

nix::Value * pythonToNixValue(nix::EvalState & state, PyObject * obj);

} // namespace nix::python
