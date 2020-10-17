#include "command.hh"
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
        addFlag({
            .longName = "substituter",
            .shortName = 's',
            .description = "use signatures from specified store",
            .labels = {"store-uri"},
            .handler = {[&](std::string s) { substituterUris.push_back(s); }},
        });
    }

    std::string description() override
    {
        return "copy path signatures from substituters (like binary caches)";
    }

    Category category() override { return catUtility; }

    void run(ref<Store> store, StorePaths storePaths) override
    {
        if (substituterUris.empty())
            throw UsageError("you must specify at least one substituter using '-s'");

        // FIXME: factor out commonality with MixVerify.
        std::vector<ref<Store>> substituters;
        for (auto & s : substituterUris)
            substituters.push_back(openStore(s));

        ThreadPool pool;

        std::string doneLabel = "done";
        std::atomic<size_t> added{0};

        //logger->setExpected(doneLabel, storePaths.size());

        auto doPath = [&](const Path & storePathS) {
            //Activity act(*logger, lvlInfo, format("getting signatures for '%s'") % storePath);

            checkInterrupt();

            auto storePath = store->parseStorePath(storePathS);

            auto info = store->queryPathInfo(storePath);

            StringSet newSigs;

            for (auto & store2 : substituters) {
                try {
                    auto info2 = store2->queryPathInfo(info->path);

                    /* Don't import signatures that don't match this
                       binary. */
                    if (info->narHash != info2->narHash ||
                        info->narSize != info2->narSize ||
                        info->references != info2->references ||
                        info->hasSelfReference != info2->hasSelfReference)
                        continue;

                    for (auto & sig : info2->sigs)
                        if (!info->sigs.count(sig))
                            newSigs.insert(sig);
                } catch (InvalidPath &) {
                }
            }

            if (!newSigs.empty()) {
                store->addSignatures(storePath, newSigs);
                added += newSigs.size();
            }

            //logger->incProgress(doneLabel);
        };

        for (auto & storePath : storePaths)
            pool.enqueue(std::bind(doPath, store->printStorePath(storePath)));

        pool.process();

        printInfo("imported %d signatures", added);
    }
};

static auto rCmdCopySigs = registerCommand<CmdCopySigs>("copy-sigs");

struct CmdSignPaths : StorePathsCommand
{
    Path secretKeyFile;

    CmdSignPaths()
    {
        addFlag({
            .longName = "key-file",
            .shortName = 'k',
            .description = "file containing the secret signing key",
            .labels = {"file"},
            .handler = {&secretKeyFile},
            .completer = completePath
        });
    }

    std::string description() override
    {
        return "sign the specified paths";
    }

    Category category() override { return catUtility; }

    void run(ref<Store> store, StorePaths storePaths) override
    {
        if (secretKeyFile.empty())
            throw UsageError("you must specify a secret key file using '-k'");

        SecretKey secretKey(readFile(secretKeyFile));

        size_t added{0};

        for (auto & storePath : storePaths) {
            auto info = store->queryPathInfo(storePath);

            auto info2(*info);
            info2.sigs.clear();
            info2.sign(*store, secretKey);
            assert(!info2.sigs.empty());

            if (!info->sigs.count(*info2.sigs.begin())) {
                store->addSignatures(storePath, info2.sigs);
                added++;
            }
        }

        printInfo("added %d signatures", added);
    }
};

static auto rCmdSignPaths = registerCommand<CmdSignPaths>("sign-paths");
