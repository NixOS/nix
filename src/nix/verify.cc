#include "affinity.hh" // FIXME
#include "command.hh"
#include "progress-bar.hh"
#include "shared.hh"
#include "store-api.hh"
#include "sync.hh"
#include "thread-pool.hh"

#include <atomic>

using namespace nix;

struct MixVerify : virtual Args
{
    bool noContents = false;
    bool noSigs = false;

    MixVerify()
    {
        mkFlag(0, "no-contents", "do not verify the contents of each store path", &noContents);
        mkFlag(0, "no-sigs", "do not verify whether each store path has a valid signature", &noSigs);
    }

    void verifyPaths(ref<Store> store, const Paths & storePaths)
    {
        restoreAffinity(); // FIXME

        auto publicKeys = getDefaultPublicKeys();

        std::atomic<size_t> untrusted{0};
        std::atomic<size_t> corrupted{0};
        std::atomic<size_t> done{0};
        std::atomic<size_t> failed{0};

        ProgressBar progressBar;

        auto showProgress = [&](bool final) {
            std::string s;
            if (final)
                s = (format("checked %d paths") % storePaths.size()).str();
            else
                s = (format("[%d/%d checked") % done % storePaths.size()).str();
            if (corrupted > 0)
                s += (format(", %d corrupted") % corrupted).str();
            if (untrusted > 0)
                s += (format(", %d untrusted") % untrusted).str();
            if (failed > 0)
                s += (format(", %d failed") % failed).str();
            if (!final) s += "]";
            return s;
        };

        progressBar.updateStatus(showProgress(false));

        ThreadPool pool;

        auto doPath = [&](const Path & storePath) {
            try {
                progressBar.startActivity(format("checking ‘%s’") % storePath);

                auto info = store->queryPathInfo(storePath);

                if (!noContents) {

                    HashSink sink(info.narHash.type);
                    store->narFromPath(storePath, sink);

                    auto hash = sink.finish();

                    if (hash.first != info.narHash) {
                        corrupted = 1;
                        printMsg(lvlError,
                            format("path ‘%s’ was modified! expected hash ‘%s’, got ‘%s’")
                            % storePath % printHash(info.narHash) % printHash(hash.first));
                    }

                }

                if (!noSigs) {

                    if (!info.ultimate && !info.checkSignatures(publicKeys)) {
                        untrusted++;
                        printMsg(lvlError, format("path ‘%s’ is untrusted") % storePath);
                    }

                }

                done++;

                progressBar.updateStatus(showProgress(false));

            } catch (Error & e) {
                printMsg(lvlError, format(ANSI_RED "error:" ANSI_NORMAL " %s") % e.what());
                failed++;
            }
        };

        for (auto & storePath : storePaths)
            pool.enqueue(std::bind(doPath, storePath));

        pool.process();

        progressBar.done();

        printMsg(lvlInfo, showProgress(true));

        throw Exit(
            (corrupted ? 1 : 0) |
            (untrusted ? 2 : 0) |
            (failed ? 4 : 0));
    }
};

struct CmdVerifyPaths : StorePathsCommand, MixVerify
{
    CmdVerifyPaths()
    {
    }

    std::string name() override
    {
        return "verify-paths";
    }

    std::string description() override
    {
        return "verify the integrity of store paths";
    }

    void run(ref<Store> store, Paths storePaths) override
    {
        verifyPaths(store, storePaths);
    }
};

static RegisterCommand r1(make_ref<CmdVerifyPaths>());

struct CmdVerifyStore : StoreCommand, MixVerify
{
    CmdVerifyStore()
    {
    }

    std::string name() override
    {
        return "verify-store";
    }

    std::string description() override
    {
        return "verify the integrity of all paths in the Nix store";
    }

    void run(ref<Store> store) override
    {
        // FIXME: use store->verifyStore()?

        PathSet validPaths = store->queryAllValidPaths();

        verifyPaths(store, Paths(validPaths.begin(), validPaths.end()));
    }
};

static RegisterCommand r2(make_ref<CmdVerifyStore>());
