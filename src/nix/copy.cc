#include "command.hh"
#include "progress-bar.hh"
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

        ProgressBar progressBar;

        std::atomic<size_t> done{0};
        std::atomic<size_t> total{storePaths.size()};

        auto showProgress = [&]() {
            return (format("[%d/%d copied]") % done % total).str();
        };

        progressBar.updateStatus(showProgress());

        struct Graph
        {
            std::set<Path> left;
            std::map<Path, std::set<Path>> refs, rrefs;
        };

        Sync<Graph> graph_;
        {
            auto graph(graph_.lock());
            graph->left = PathSet(storePaths.begin(), storePaths.end());
        }

        ThreadPool pool;

        std::function<void(const Path &)> doPath;

        doPath = [&](const Path & storePath) {
            checkInterrupt();

            if (!dstStore->isValidPath(storePath)) {
                auto activity(progressBar.startActivity(format("copying ‘%s’...") % storePath));

                StringSink sink;
                srcStore->exportPaths({storePath}, false, sink);

                StringSource source(*sink.s);
                dstStore->importPaths(false, source, 0);

                done++;
            } else
                total--;

            progressBar.updateStatus(showProgress());

            /* Enqueue all paths that were waiting for this one. */
            {
                auto graph(graph_.lock());
                graph->left.erase(storePath);
                for (auto & rref : graph->rrefs[storePath]) {
                    auto & refs(graph->refs[rref]);
                    auto i = refs.find(storePath);
                    assert(i != refs.end());
                    refs.erase(i);
                    if (refs.empty())
                        pool.enqueue(std::bind(doPath, rref));
                }
            }
        };

        /* Build the dependency graph; enqueue all paths with no
           dependencies. */
        for (auto & storePath : storePaths) {
            auto info = srcStore->queryPathInfo(storePath);
            {
                auto graph(graph_.lock());
                for (auto & ref : info->references)
                    if (ref != storePath && graph->left.count(ref)) {
                        graph->refs[storePath].insert(ref);
                        graph->rrefs[ref].insert(storePath);
                    }
                if (graph->refs[storePath].empty())
                    pool.enqueue(std::bind(doPath, storePath));
            }
        }

        pool.process();

        progressBar.done();
    }
};

static RegisterCommand r1(make_ref<CmdCopy>());
