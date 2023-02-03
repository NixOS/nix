#include "installable-derived-path.hh"
#include "derivations.hh"

namespace nix {

std::string InstallableDerivedPath::what() const
{
    return derivedPath.to_string(*store);
}

DerivedPathsWithInfo InstallableDerivedPath::toDerivedPaths()
{
    return {{.path = derivedPath, .info = {} }};
}

std::optional<StorePath> InstallableDerivedPath::getStorePath()
{
    return std::visit(overloaded {
        [&](const DerivedPath::Built & bfd) {
            return bfd.drvPath;
        },
        [&](const DerivedPath::Opaque & bo) {
            return bo.path;
        },
    }, derivedPath.raw());
}

InstallableDerivedPath InstallableDerivedPath::parse(
    ref<Store> store,
    std::string_view prefix,
    ExtendedOutputsSpec extendedOutputsSpec)
{
    auto derivedPath = std::visit(overloaded {
        // If the user did not use ^, we treat the output more liberally.
        [&](const ExtendedOutputsSpec::Default &) -> DerivedPath {
            // First, we accept a symlink chain or an actual store path.
            auto storePath = store->followLinksToStorePath(prefix);
            // Second, we see if the store path ends in `.drv` to decide what sort
            // of derived path they want.
            //
            // This handling predates the `^` syntax. The `^*` in
            // `/nix/store/hash-foo.drv^*` unambiguously means "do the
            // `DerivedPath::Built` case", so plain `/nix/store/hash-foo.drv` could
            // also unambiguously mean "do the DerivedPath::Opaque` case".
            //
            // Issue #7261 tracks reconsidering this `.drv` dispatching.
            return storePath.isDerivation()
                ? (DerivedPath) DerivedPath::Built {
                    .drvPath = std::move(storePath),
                    .outputs = OutputsSpec::All {},
                }
                : (DerivedPath) DerivedPath::Opaque {
                    .path = std::move(storePath),
                };
        },
        // If the user did use ^, we just do exactly what is written.
        [&](const ExtendedOutputsSpec::Explicit & outputSpec) -> DerivedPath {
            return DerivedPath::Built {
                .drvPath = store->parseStorePath(prefix),
                .outputs = outputSpec,
            };
        },
    }, extendedOutputsSpec.raw());
    return InstallableDerivedPath {
        store,
        std::move(derivedPath),
    };
}

}
