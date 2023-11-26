#pragma once

#include "input-accessor.hh"

namespace nix {

ref<InputAccessor> makeMountedInputAccessor(std::map<CanonPath, ref<InputAccessor>> mounts);

}
