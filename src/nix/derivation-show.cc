// FIXME: integrate this with `nix path-info`?
// FIXME: rename to 'nix store derivation show'?

#include "nix/cmd/command.hh"
#include "nix/main/common-args.hh"
#include "nix/store/store-api.hh"
#include "nix/store/derivations.hh"
#include "nix/store/derivation/aterm.hh"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace nix {

struct CmdShowDerivation : InstallablesCommand, MixPrintJSON
{
    bool recursive = false;
    std::optional<std::string> atermStdinName;

    CmdShowDerivation()
    {
        addFlag({
            .longName = "recursive",
            .shortName = 'r',
            .description = "Include the dependencies of the specified derivations.",
            .handler = {&recursive, true},
        });
        addFlag({
            .longName = "aterm-stdin",
            .description = "Read a single derivation in the ATerm format from standard input instead of taking "
                           "installables. The ATerm format does not record the derivation's name, so it must be given.",
            .labels = {"name"},
            .handler = {&atermStdinName},
        });
    }

    void applyDefaultInstallables(std::vector<std::string> & rawInstallables) override
    {
        if (!atermStdinName)
            InstallablesCommand::applyDefaultInstallables(rawInstallables);
    }

    std::string description() override
    {
        return "show the contents of a store derivation";
    }

    std::string doc() override
    {
        return
#include "derivation-show.md"
            ;
    }

    Category category() override
    {
        return catUtility;
    }

    void run(ref<Store> store, Installables && installables) override
    {
        json jsonRoot = json::object();

        if (atermStdinName) {
            if (!installables.empty())
                throw UsageError("'--aterm-stdin' cannot be combined with installables");
            if (recursive)
                throw UsageError("'--aterm-stdin' cannot be combined with '--recursive'");
            auto drv = derivation::parse(*store, drainFD(STDIN_FILENO), *atermStdinName);
            auto drvPath = computeStorePath(*store, drv);
            jsonRoot[drvPath.to_string()] = std::move(drv);
        } else {
            auto drvPaths = Installable::toDerivations(store, installables, true);

            if (recursive) {
                StorePathSet closure;
                store->computeFSClosure(drvPaths, closure);
                drvPaths = std::move(closure);
            }

            for (auto & drvPath : drvPaths) {
                if (!drvPath.isDerivation())
                    continue;

                jsonRoot[drvPath.to_string()] = store->readDerivation(drvPath);
            }
        }
        printJSON(
            nlohmann::json{
                {"version", expectedJsonVersionDerivation},
                {"derivations", std::move(jsonRoot)},
            });
    }
};

static auto rCmdShowDerivation = registerCommand2<CmdShowDerivation>({"derivation", "show"});

} // namespace nix
