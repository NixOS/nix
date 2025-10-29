#include "nix/cmd/installable-derived-path.hh"
#include "nix/store/derivations.hh"

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

InstallableDerivedPath
InstallableDerivedPath::parse(ref<Store> store, std::string_view prefix, ExtendedOutputsSpec extendedOutputsSpec)
{
    auto derivedPath = std::visit(
        overloaded{
            // If the user did not use ^, we treat the output more
            // liberally: we accept a symlink chain or an actual
            // store path.
            [&](const ExtendedOutputsSpec::Default &) -> DerivedPath {
                auto storePath = store->followLinksToStorePath(prefix);
                return DerivedPath::Opaque{
                    .path = std::move(storePath),
                };
            },
            // If the user did use ^, we just do exactly what is written.
            [&](const ExtendedOutputsSpec::Explicit & outputSpec) -> DerivedPath {
                auto drv = make_ref<SingleDerivedPath>(SingleDerivedPath::parse(*store, prefix));
                drvRequireExperiment(*drv);
                return DerivedPath::Built{
                    .drvPath = std::move(drv),
                    .outputs = outputSpec,
                };
            },
        },
        extendedOutputsSpec.raw);
    return InstallableDerivedPath{
        store,
        std::move(derivedPath),
    };
}

} // namespace nix
