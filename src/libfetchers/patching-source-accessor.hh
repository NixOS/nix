#pragma once

#include "source-accessor.hh"

namespace nix {

ref<SourceAccessor> makePatchingSourceAccessor(ref<SourceAccessor> next, const std::vector<std::string> & patches);

}
