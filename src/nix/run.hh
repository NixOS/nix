#pragma once
///@file

#include "store-api.hh"

namespace nix {

enum struct UseLookupPath {
    Use,
    DontUse
};

void runProgramInStore(ref<Store> store,
    UseLookupPath useLookupPath,
    const std::string & program,
    const Strings & args,
    std::optional<std::string_view> system = std::nullopt);

}
