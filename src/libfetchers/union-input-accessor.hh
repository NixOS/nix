#pragma once

#include "input-accessor.hh"

namespace nix {

ref<InputAccessor> makeUnionInputAccessor(std::map<CanonPath, ref<InputAccessor>> mounts);

}
