#pragma once
///@file

#include "nix/store/store-api.hh"

namespace nix {

enum struct UseLookupPath { Use, DontUse };

void execProgramInStore(
    ref<Store> store,
    UseLookupPath useLookupPath,
    const std::string & program,
    const Strings & args,
    std::optional<std::string_view> system = std::nullopt,
    std::optional<StringMap> env = std::nullopt);

} // namespace nix
