#pragma once
///@file

#include "nix/util/error.hh"

namespace nix {

MakeError(SubstituteGone, Error);

enum SubstituteFlag : bool { NoSubstitute = false, Substitute = true };

} // namespace nix
