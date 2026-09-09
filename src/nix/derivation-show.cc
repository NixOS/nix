// FIXME: integrate this with `nix path-info`?
// FIXME: rename to 'nix store derivation show'?

#include "nix/cmd/command.hh"
#include "nix/main/common-args.hh"
#include "nix/store/store-api.hh"
#include "nix/store/derivations.hh"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace nix {

struct CmdShowDerivation : InstallablesCommand, MixPrintJSON
{
    bool recursive = false;
    std::optional<derivation::JsonFormat> jsonFormat;

    CmdShowDerivation()
    {
        addFlag({
            .longName = "recursive",
            .shortName = 'r',
            .description = "Include the dependencies of the specified derivations.",
            .handler = {&recursive, true},
        });

        addFlag({
            .longName = "json-format",
            .description =
                "JSON format version of [derivation](@docroot@/protocols/json/derivation/index.md) to use (4 or 5).\n"
                "Version 4 only encodes the derivation options (and pass-as-file flags) via the environment variables.\n"
                "Version 5 represents the derivation options first-class.",
            .labels = {"version"},
            .handler = {[this](std::string s) {
                jsonFormat = derivation::parseJsonFormat(string2IntWithUnitPrefix<uint64_t>(s));
            }},
        });
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
        auto drvPaths = Installable::toDerivations(store, installables, true);

        if (recursive) {
            StorePathSet closure;
            store->computeFSClosure(drvPaths, closure);
            drvPaths = std::move(closure);
        }

        auto format = jsonFormat.value_or(derivation::JsonFormat::V5);

        json jsonRoot = json::object();

        for (auto & drvPath : drvPaths) {
            if (!drvPath.isDerivation())
                continue;

            jsonRoot[drvPath.to_string()] = derivation::toJSON(store->readDerivation(drvPath), format);
        }
        printJSON(
            nlohmann::json{
                {"version", static_cast<uint64_t>(format)},
                {"derivations", std::move(jsonRoot)},
            });
    }
};

static auto rCmdShowDerivation = registerCommand2<CmdShowDerivation>({"derivation", "show"});

} // namespace nix
