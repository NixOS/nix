#pragma once

#include "nix/expr/eval.hh"
#include "nix/flake/settings.hh"

namespace nix::flake::primops {

/**
 * Returns a `builtins.getFlake` primop with the given nix::flake::Settings.
 */
nix::PrimOp getFlake(const Settings & settings);

extern nix::PrimOp parseFlakeRef;
extern nix::PrimOp flakeRefToString;

} // namespace nix::flake::primops
