#include "command.hh"
#include "progress-bar.hh"
#include "shared.hh"
#include "store-api.hh"
#include "sync.hh"

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

        ProgressBar progressBar;

        std::atomic<size_t> done{0};
        std::atomic<size_t> total{storePaths.size()};

        auto showProgress = [&]() {
            return (format("[%d/%d copied]") % done % total).str();
        };

        progressBar.updateStatus(showProgress());

        storePaths.reverse(); // FIXME: assumes reverse topo sort

        for (auto & storePath : storePaths) {
            checkInterrupt();

            if (dstStore->isValidPath(storePath)) {
                total--;
                progressBar.updateStatus(showProgress());
                continue;
            }

            auto activity(progressBar.startActivity(format("copying ‘%s’...") % storePath));

            StringSink sink;
            srcStore->exportPaths({storePath}, false, sink);

            StringSource source(*sink.s);
            dstStore->importPaths(false, source, 0);

            done++;
            progressBar.updateStatus(showProgress());
        }

        progressBar.done();
    }
};

static RegisterCommand r1(make_ref<CmdCopy>());
