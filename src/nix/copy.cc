#include "command.hh"
#include "shared.hh"
#include "store-api.hh"
#include "sync.hh"
#include "thread-pool.hh"

#include <atomic>

using namespace nix;

struct CmdCopy : StorePathsCommand
{
    std::string srcUri, dstUri;

    CheckSigsFlag checkSigs = CheckSigs;

    CmdCopy()
    {
        mkFlag(0, "from", "store-uri", "URI of the source Nix store", &srcUri);
        mkFlag(0, "to", "store-uri", "URI of the destination Nix store", &dstUri);

        mkFlag()
            .longName("no-check-sigs")
            .description("do not require that paths are signed by trusted keys")
            .set(&checkSigs, NoCheckSigs);
    }

    std::string name() override
    {
        return "copy";
    }

    std::string description() override
    {
        return "copy paths between Nix stores";
    }

    Examples examples() override
    {
        return {
            Example{
                "To copy Firefox to the local store to a binary cache in file:///tmp/cache:",
                "nix copy --to file:///tmp/cache -r $(type -p firefox)"
            },
        };
    }

    ref<Store> createStore() override
    {
        return srcUri.empty() ? StoreCommand::createStore() : openStore(srcUri);
    }

    void run(ref<Store> srcStore, Paths storePaths) override
    {
        if (srcUri.empty() && dstUri.empty())
            throw UsageError("you must pass '--from' and/or '--to'");

        ref<Store> dstStore = dstUri.empty() ? openStore() : openStore(dstUri);

        copyPaths(srcStore, dstStore, PathSet(storePaths.begin(), storePaths.end()),
            NoRepair, checkSigs);
    }
};

static RegisterCommand r1(make_ref<CmdCopy>());
