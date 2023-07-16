#pragma once

#include <Python.h>

#include <eval.hh>

namespace nix::python {

PyObject * nixToPythonObject(nix::EvalState & state, nix::Value & v, nix::PathSet & context);
PyObject * _nixToPythonObject(nix::EvalState & state, nix::Value & v, nix::PathSet & context, std::set<const void *> seen);
} // namespace nix::python
