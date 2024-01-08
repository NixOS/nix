#include "installable-derived-path.hh"
#include "derivations.hh"

namespace nix {

std::string InstallableDerivedPath::what() const
{
    return derivedPath.to_string(*store);
}

DerivedPathsWithInfo InstallableDerivedPath::toDerivedPaths()
{
    return {{
        .path = derivedPath,
        .info = make_ref<ExtraPathInfo>(),
    }};
}

std::optional<StorePath> InstallableDerivedPath::getStorePath()
{
    return derivedPath.getBaseStorePath();
}

InstallableDerivedPath InstallableDerivedPath::parse(
    ref<Store> store,
    std::string_view prefix,
    ExtendedOutputsSpec extendedOutputsSpec)
{
    auto derivedPath = std::visit(overloaded {
        // If the user did not use ^, we treat the output more
        // liberally: we accept a symlink chain or an actual
        // store path.
        [&](const ExtendedOutputsSpec::Default &) -> DerivedPath {
            auto storePath = store->followLinksToStorePath(prefix);
            // Remove this prior to stabilizing the new CLI.
            if (storePath.isDerivation()) {
                auto oldDerivedPath = DerivedPath::Built {
                    .drvPath = makeConstantStorePathRef(storePath),
                    .outputs = OutputsSpec::All { },
                };
                warn(
                    "The interpretation of store paths arguments ending in `.drv` recently changed. If this command is now failing try again with '%s'",
                    oldDerivedPath.to_string(*store));
            };
            return DerivedPath::Opaque {
                .path = std::move(storePath),
            };
        },
        // If the user did use ^, we just do exactly what is written.
        [&](const ExtendedOutputsSpec::Explicit & outputSpec) -> DerivedPath {
            auto drv = make_ref<SingleDerivedPath>(SingleDerivedPath::parse(*store, prefix));
            drvRequireExperiment(*drv);
            return DerivedPath::Built {
                .drvPath = std::move(drv),
                .outputs = outputSpec,
            };
        },
    }, extendedOutputsSpec.raw);
    return InstallableDerivedPath {
        store,
        std::move(derivedPath),
    };
}

}
