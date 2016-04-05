#include "affinity.hh" // FIXME
#include "command.hh"
#include "progress-bar.hh"
#include "shared.hh"
#include "store-api.hh"
#include "thread-pool.hh"

#include <atomic>

using namespace nix;

struct CmdCopySigs : StorePathsCommand
{
    Strings substituterUris;

    CmdCopySigs()
    {
        mkFlag('s', "substituter", {"store-uri"}, "use signatures from specified store", 1,
            [&](Strings ss) { substituterUris.push_back(ss.front()); });
    }

    std::string name() override
    {
        return "copy-sigs";
    }

    std::string description() override
    {
        return "copy path signatures from substituters (like binary caches)";
    }

    void run(ref<Store> store, Paths storePaths) override
    {
        restoreAffinity(); // FIXME

        if (substituterUris.empty())
            throw UsageError("you must specify at least one substituter using ‘-s’");

        // FIXME: factor out commonality with MixVerify.
        std::vector<ref<Store>> substituters;
        for (auto & s : substituterUris)
            substituters.push_back(openStoreAt(s));

        ProgressBar progressBar;

        ThreadPool pool;

        std::atomic<size_t> done{0};
        std::atomic<size_t> added{0};

        auto showProgress = [&]() {
            return (format("[%d/%d done]") % done % storePaths.size()).str();
        };

        progressBar.updateStatus(showProgress());

        auto doPath = [&](const Path & storePath) {
            auto activity(progressBar.startActivity(format("getting signatures for ‘%s’") % storePath));

            checkInterrupt();

            auto info = store->queryPathInfo(storePath);

            StringSet newSigs;

            for (auto & store2 : substituters) {
                if (!store2->isValidPath(storePath)) continue;
                auto info2 = store2->queryPathInfo(storePath);

                /* Don't import signatures that don't match this
                   binary. */
                if (info.narHash != info2.narHash ||
                    info.narSize != info2.narSize ||
                    info.references != info2.references)
                    continue;

                for (auto & sig : info2.sigs)
                    if (!info.sigs.count(sig))
                        newSigs.insert(sig);
            }

            if (!newSigs.empty()) {
                store->addSignatures(storePath, newSigs);
                added += newSigs.size();
            }

            done++;
            progressBar.updateStatus(showProgress());
        };

        for (auto & storePath : storePaths)
            pool.enqueue(std::bind(doPath, storePath));

        pool.process();

        progressBar.done();

        printMsg(lvlInfo, format("imported %d signatures") % added);
    }
};

static RegisterCommand r1(make_ref<CmdCopySigs>());

struct CmdQueryPathSigs : StorePathsCommand
{
    CmdQueryPathSigs()
    {
    }

    std::string name() override
    {
        return "query-path-sigs";
    }

    std::string description() override
    {
        return "print store path signatures";
    }

    void run(ref<Store> store, Paths storePaths) override
    {
        for (auto & storePath : storePaths) {
            auto info = store->queryPathInfo(storePath);
            std::cout << storePath << " ";
            if (info.ultimate) std::cout << "ultimate ";
            for (auto & sig : info.sigs)
                std::cout << sig << " ";
            std::cout << "\n";
        }
    }
};

static RegisterCommand r2(make_ref<CmdQueryPathSigs>());

struct CmdSignPaths : StorePathsCommand
{
    Path secretKeyFile;

    CmdSignPaths()
    {
        mkFlag('k', "key-file", {"file"}, "file containing the secret signing key", &secretKeyFile);
    }

    std::string name() override
    {
        return "sign-paths";
    }

    std::string description() override
    {
        return "sign the specified paths";
    }

    void run(ref<Store> store, Paths storePaths) override
    {
        if (secretKeyFile.empty())
            throw UsageError("you must specify a secret key file using ‘-k’");

        SecretKey secretKey(readFile(secretKeyFile));

        size_t added{0};

        for (auto & storePath : storePaths) {
            auto info = store->queryPathInfo(storePath);

            auto info2(info);
            info2.sigs.clear();
            info2.sign(secretKey);
            assert(!info2.sigs.empty());

            if (!info.sigs.count(*info2.sigs.begin())) {
                store->addSignatures(storePath, info2.sigs);
                added++;
            }
        }

        printMsg(lvlInfo, format("added %d signatures") % added);
    }
};

static RegisterCommand r3(make_ref<CmdSignPaths>());
