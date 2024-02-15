#pragma once

#include "input-accessor.hh"

namespace nix {

ref<InputAccessor> makePatchingInputAccessor(
    ref<InputAccessor> next,
    const std::vector<std::string> & patches);

}
