#include "nix/cmd/command.hh"
#include "nix/cmd/installable-flake.hh"
#include "nix/main/common-args.hh"
#include "nix/main/shared.hh"
#include "nix/store/store-api.hh"
#include "nix/store/globals.hh"

using namespace nix;

struct CmdCheck : InstallablesCommand, MixDryRun
{
    std::string description() override
    {
        return "check that a derivation can be built or substituted";
    }

    std::string doc() override
    {
        return
#include "check.md"
            ;
    }

    Strings getDefaultFlakeAttrPaths() override
    {
        return {};
    }

    Strings getDefaultFlakeAttrPathPrefixes() override
    {
        return {
            "checks." + settings.thisSystem.get() + ".",
            "packages." + settings.thisSystem.get() + ".",
            "legacyPackages." + settings.thisSystem.get() + "."};
    }

    void applyDefaultInstallables(std::vector<std::string> & rawInstallables) override
    {
        if (rawInstallables.empty())
            throw UsageError(
                "'nix check' requires at least one installable argument.\n\nDid you mean 'nix flake check'?");
    }

    void run(ref<Store> store, Installables && installables) override
    {
        // Detect bare flake references without fragments
        for (auto & installable : installables) {
            if (auto installableFlake = installable.dynamic_pointer_cast<InstallableFlake>()) {
                if (installableFlake->attrPaths.empty()) {
                    throw Error(
                        "Installable '%s' does not specify which outputs to check.\n"
                        "Use '%s#<output>' to check a specific output, or 'nix flake check %s' to check all outputs.",
                        installableFlake->flakeRef.to_string(),
                        installableFlake->flakeRef.to_string(),
                        installableFlake->flakeRef.to_string());
                }
            }
        }

        std::vector<DerivedPath> pathsToCheck;

        for (auto & i : installables)
            for (auto & b : i->toDerivedPaths())
                pathsToCheck.push_back(b.path);

        // Query what needs to be built vs what can be substituted
        auto missing = store->queryMissing(pathsToCheck);

        if (dryRun) {
            // Use lvlError to always show the output
            printMissing(store, missing, lvlError, /* intendToRealise = */ false);
            return;
        }

        // Only build what cannot be substituted
        std::vector<DerivedPath> toBuild;
        // Convert derivation store paths to DerivedPath::Built (same pattern as nix flake check)
        for (auto & path : missing.willBuild) {
            toBuild.emplace_back(
                DerivedPath::Built{
                    .drvPath = makeConstantStorePathRef(path),
                    .outputs = OutputsSpec::All{},
                });
        }

        if (!toBuild.empty()) {
            // TODO: Add a Realise mode that performs the build but does not copy
            // the outputs to the local store (for remote builders).
            store->buildPaths(toBuild);
        }

        // Report success for all checked paths
        for (auto & path : pathsToCheck) {
            std::visit(
                overloaded{
                    [&](const DerivedPath::Opaque & bo) {
                        logger->log(lvlNotice, fmt("%s: OK (opaque path)", store->printStorePath(bo.path)));
                    },
                    [&](const DerivedPath::Built & bfd) {
                        auto drvPath = resolveDerivedPath(*store, *bfd.drvPath);
                        logger->log(lvlNotice, fmt("%s: OK (available)", store->printStorePath(drvPath)));
                    },
                },
                path.raw());
        }
    }
};

static auto rCmdCheck = registerCommand<CmdCheck>("check");
