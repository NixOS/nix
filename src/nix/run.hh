#pragma once

#include "store-api.hh"

namespace nix {

void runProgramInStore(ref<Store> store,
    const std::string & program,
    const Strings & args);

}
