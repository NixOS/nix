#include "nix/store/derivation/output.hh"
#include "nix/store/store-api.hh"
#include "nix/util/util.hh"

namespace nix {

std::optional<StorePath>
DerivationOutput::path(const StoreDirConfig & store, std::string_view drvName, OutputNameView outputName) const
{
    return std::visit(
        overloaded{
            [](const DerivationOutput::InputAddressed & doi) -> std::optional<StorePath> { return {doi.path}; },
            [&](const DerivationOutput::CAFixed & dof) -> std::optional<StorePath> {
                return {dof.path(store, drvName, outputName)};
            },
            [](const DerivationOutput::CAFloating & dof) -> std::optional<StorePath> { return std::nullopt; },
            [](const DerivationOutput::Deferred &) -> std::optional<StorePath> { return std::nullopt; },
            [](const DerivationOutput::Impure &) -> std::optional<StorePath> { return std::nullopt; },
        },
        raw);
}

StorePath
DerivationOutput::CAFixed::path(const StoreDirConfig & store, std::string_view drvName, OutputNameView outputName) const
{
    return store.makeFixedOutputPathFromCA(
        outputPathName(drvName, outputName), ContentAddressWithReferences::withoutRefs(ca));
}

std::string outputPathName(std::string_view drvName, OutputNameView outputName)
{
    using namespace std::literals::string_view_literals;

    std::string res{drvName};
    if (outputName != "out"sv) {
        res += '-';
        res += outputName;
    }
    return res;
}

} // namespace nix
