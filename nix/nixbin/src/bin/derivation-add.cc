// FIXME: rename to 'nix plan add' or 'nix derivation add'?

#include "command.hh"
#include "common-args.hh"
#include "store-api.hh"
#include "archive.hh"
#include "derivations.hh"
#include <nlohmann/json.hpp>

using namespace nix;
using json = nlohmann::json;

struct CmdAddDerivation : MixDryRun, StoreCommand
{
    std::string description() override
    {
        return "Add a store derivation";
    }

    std::string doc() override
    {
        return
          #include "derivation-add.md"
          ;
    }

    Category category() override { return catUtility; }

    void run(ref<Store> store) override
    {
        auto json = nlohmann::json::parse(drainFD(STDIN_FILENO));

        auto drv = Derivation::fromJSON(*store, json);

        auto drvPath = writeDerivation(*store, drv, NoRepair, /* read only */ dryRun);

        drv.checkInvariants(*store, drvPath);

        writeDerivation(*store, drv, NoRepair, dryRun);

        logger->cout("%s", store->printStorePath(drvPath));
    }
};

static auto rCmdAddDerivation = registerCommand2<CmdAddDerivation>({"derivation", "add"});
