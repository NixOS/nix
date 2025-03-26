#pragma once

#include "eval.hh"
#include "flake/settings.hh"

namespace nix::flake::primops {

/**
 * Returns a `builtins.getFlake` primop with the given nix::flake::Settings.
 */
nix::PrimOp getFlake(const Settings & settings);

} // namespace nix::flake