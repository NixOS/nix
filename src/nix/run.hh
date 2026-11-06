#pragma once
///@file

#include "nix/util/ref.hh"
#include "nix/util/types.hh"

#include <optional>
#include <string>
#include <string_view>

namespace nix {

class Store;

enum struct UseLookupPath { Use, DontUse };

void execProgramInStore(
    ref<Store> store,
    UseLookupPath useLookupPath,
    const std::string & program,
    const Strings & args,
    std::optional<std::string_view> system = std::nullopt,
    std::optional<StringMap> env = std::nullopt);

} // namespace nix
