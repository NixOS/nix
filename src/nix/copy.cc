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

    CmdCopy()
    {
        mkFlag(0, "from", "store-uri", "URI of the source Nix store", &srcUri);
        mkFlag(0, "to", "store-uri", "URI of the destination Nix store", &dstUri);
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

    void run(ref<Store> store, Paths storePaths) override
    {
        if (srcUri.empty() && dstUri.empty())
            throw UsageError("you must pass ‘--from’ and/or ‘--to’");

        ref<Store> srcStore = srcUri.empty() ? store : openStoreAt(srcUri);
        ref<Store> dstStore = dstUri.empty() ? store : openStoreAt(dstUri);

        std::string copiedLabel = "copied";

        logger->setExpected(copiedLabel, storePaths.size());

        ThreadPool pool;

        processGraph<Path>(pool,
            PathSet(storePaths.begin(), storePaths.end()),

            [&](const Path & storePath) {
                return srcStore->queryPathInfo(storePath)->references;
            },

            [&](const Path & storePath) {
                checkInterrupt();

                if (!dstStore->isValidPath(storePath)) {
                    Activity act(*logger, lvlInfo, format("copying ‘%s’...") % storePath);

                    copyStorePath(srcStore, dstStore, storePath);

                    logger->incProgress(copiedLabel);
                } else
                    logger->incExpected(copiedLabel, -1);
            });

        pool.process();
    }
};

static RegisterCommand r1(make_ref<CmdCopy>());
