#pragma once
///@file

#include "nix/cmd/installables.hh"
#include "nix/util/ref.hh"

#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace nix {

class Store;
class StorePath;
struct DerivedPath;
struct ExtendedOutputsSpec;

struct InstallableDerivedPath : Installable
{
    ref<Store> store;
    DerivedPath derivedPath;

    InstallableDerivedPath(ref<Store> store, DerivedPath && derivedPath)
        : store(store)
        , derivedPath(std::move(derivedPath))
    {
    }

    std::string what() const override;

    DerivedPathsWithInfo toDerivedPaths() override;

    std::optional<StorePath> getStorePath() override;

    static InstallableDerivedPath
    parse(ref<Store> store, std::string_view prefix, ExtendedOutputsSpec extendedOutputsSpec);
};

} // namespace nix
