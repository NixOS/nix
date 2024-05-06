#pragma once

#include "source-accessor.hh"

namespace nix {

ref<SourceAccessor> makeMountedInputAccessor(std::map<CanonPath, ref<SourceAccessor>> mounts);

}
