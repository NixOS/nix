#pragma once

#include <Python.h>
#include <config.h>

#include <eval.hh>
#include <optional>

namespace pythonnix {

nix::Value *pythonToNixValue(nix::EvalState &state, PyObject *obj);

std::optional<nix::StaticEnv> pythonToNixEnv(nix::EvalState &state,
                                             PyObject *vars, nix::Env **env);
} // namespace pythonnix
