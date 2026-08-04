#include "nix/store/derivation/output.hh"
#include "nix/store/derivations.hh"
#include "nix/store/store-api.hh"
#include "nix/util/util.hh"

namespace nix {

namespace derivation {

std::optional<StorePath>
Output::path(const StoreDirConfig & store, std::string_view drvName, OutputNameView outputName) const
{
    return std::visit(
        overloaded{
            [](const Output::InputAddressed & doi) -> std::optional<StorePath> { return {doi.path}; },
            [&](const Output::CAFixed & dof) -> std::optional<StorePath> {
                return {dof.path(store, drvName, outputName)};
            },
            [](const Output::CAFloating & dof) -> std::optional<StorePath> { return std::nullopt; },
            [](const Output::Deferred &) -> std::optional<StorePath> { return std::nullopt; },
            [](const Output::Impure &) -> std::optional<StorePath> { return std::nullopt; },
        },
        raw);
}

StorePath Output::CAFixed::path(const StoreDirConfig & store, std::string_view drvName, OutputNameView outputName) const
{
    return store.makeFixedOutputPathFromCA(
        outputPathName(drvName, outputName), ContentAddressWithReferences::withoutRefs(ca));
}

} // namespace derivation

} // namespace nix
