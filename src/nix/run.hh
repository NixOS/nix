#pragma once
///@file

#include "store-api.hh"

namespace nix {

enum struct UseSearchPath {
    Use,
    DontUse
};

void runProgramInStore(ref<Store> store,
    UseSearchPath useSearchPath,
    const std::string & program,
    const Strings & args,
    std::optional<std::string_view> system = std::nullopt);

}
