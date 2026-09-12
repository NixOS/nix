// FIXME: rename to 'nix plan add' or 'nix derivation add'?

#include "nix/cmd/command.hh"
#include "nix/main/common-args.hh"
#include "nix/store/store-api.hh"
#include "nix/store/derivations.hh"
#include "nix/store/derivation/cbor.hh"
#include "nix/store/globals.hh"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace nix {

struct CmdAddDerivation : MixDryRun, StoreCommand
{
    bool cbor = false;

    CmdAddDerivation()
    {
        addFlag({
            .longName = "cbor",
            .description = "Read a derivation in CBOR format version 1 from standard input.",
            .handler = {&cbor, true},
        });
    }

    std::string description() override
    {
        return "add a store derivation";
    }

    std::string doc() override
    {
        return
#include "derivation-add.md"
            ;
    }

    Category category() override
    {
        return catUtility;
    }

    void run(ref<Store> store) override
    {
        auto bytes = drainFD(STDIN_FILENO);
        auto drv = cbor ? derivation::parseCbor(bytes)
                        : derivation::parseJsonAndValidate(*store, nlohmann::json::parse(bytes));
        if (cbor) {
            derivation::fillInOutputPaths(drv, *store);
            derivation::checkInvariants(drv, *store);
        }

        auto drvPath =
            (dryRun || settings.readOnlyMode) ? computeStorePath(*store, drv) : store->writeDerivation(drv, NoRepair);

        logger->cout("%s", store->printStorePath(drvPath));
    }
};

static auto rCmdAddDerivation = registerCommand2<CmdAddDerivation>({"derivation", "add"});

} // namespace nix
