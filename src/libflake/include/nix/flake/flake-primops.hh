#pragma once

#include "nix/expr/eval.hh"

namespace nix {
namespace flake {
struct Settings;
} // namespace flake
} // namespace nix

namespace nix::flake::primops {

/**
 * Returns a `builtins.getFlake` primop with the given nix::flake::Settings.
 */
nix::PrimOp getFlake(const Settings & settings);

extern nix::PrimOp parseFlakeRef;
extern nix::PrimOp flakeRefToString;

} // namespace nix::flake::primops
