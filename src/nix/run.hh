#pragma once
///@file

#include "store-api.hh"

namespace nix {

void runProgramInStore(ref<Store> store,
    bool search,
    const std::string & program,
    const Strings & args,
    std::optional<std::string_view> system = std::nullopt);

}
