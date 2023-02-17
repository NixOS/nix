#pragma once

#include <Python.h>

#include <config.h>

#include <eval.hh>

namespace pythonnix {

PyObject * nixToPythonObject(nix::EvalState & state, nix::Value & v, nix::PathSet & context);
} // namespace pythonnix
