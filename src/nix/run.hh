#pragma once
///@file

#include "store-api.hh"

namespace nix {

void runProgramInStore(ref<Store> store,
    const std::string & program,
    const Strings & args,
    std::optional<std::string_view> system = std::nullopt);

}
