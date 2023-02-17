#include "nix/cmd/command.hh"
#include "store-api.hh"

using namespace nix;

struct CmdPathFromHashPart : StoreCommand
{
    std::string hashPart;

    CmdPathFromHashPart()
    {
        expectArgs({
            .label = "hash-part",
            .handler = {&hashPart},
        });
    }

    std::string description() override
    {
        return "get a store path from its hash part";
    }

    std::string doc() override
    {
        return
          #include "path-from-hash-part.md"
          ;
    }

    void run(ref<Store> store) override
    {
        if (auto storePath = store->queryPathFromHashPart(hashPart))
            logger->cout(store->printStorePath(*storePath));
        else
            throw Error("there is no store path corresponding to '%s'", hashPart);
    }
};

static auto rCmdPathFromHashPart = registerCommand2<CmdPathFromHashPart>({"store", "path-from-hash-part"});
