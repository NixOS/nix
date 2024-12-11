#pragma once

#include "source-accessor.hh"

namespace nix {

ref<SourceAccessor> makeMountedSourceAccessor(std::map<CanonPath, ref<SourceAccessor>> mounts);

}
