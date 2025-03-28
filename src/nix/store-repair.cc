#include "command.hh"
#include "store-api.hh"

using namespace nix;

struct CmdStoreRepair : StorePathsCommand
{
    std::string description() override
    {
        return "repair store paths";
    }

    std::string doc() override
    {
        return
          #include "store-repair.md"
          ;
    }

    void run(ref<Store> store, StorePaths && storePaths) override
    {
        size_t errors = 0;

        // Remove duplicates.
        StorePathSet storePathsSet;
        for (auto & path : storePaths)
            storePathsSet.insert(path);

        for (auto & path : storePathsSet) {
            try {
                store->repairPath(path);
            } catch (Error & e) {
                // RepairFailure already has the path in the error message, so we omit the redundant trace.
                if (!dynamic_cast<RepairFailure *>(&e))
                    e.addTrace({}, "while repairing path '%s'", store->printStorePath(path));

                if (settings.keepGoing) {
                    errors++;
                    ignoreExceptionExceptInterrupt();
                }
                else
                    throw;
            }
        }

        if (errors)
            throw Error("could not repair %d store paths", errors);
    }
};

static auto rStoreRepair = registerCommand2<CmdStoreRepair>({"store", "repair"});
