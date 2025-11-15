#include "nix/util/signals.hh"
#include "nix/cmd/command.hh"
#include "nix/main/shared.hh"
#include "nix/store/store-open.hh"
#include "nix/util/thread-pool.hh"
#include "nix/store/filetransfer.hh"

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
            .description = "Copy signatures from the specified store.",
            .labels = {"store-uri"},
            .handler = {[&](std::string s) { substituterUris.push_back(s); }},
        });
    }

    std::string description() override
    {
        return "copy store path signatures from substituters";
    }

    std::string doc() override
    {
        return
#include "store-copy-sigs.md"
            ;
    }

    void run(ref<Store> store, StorePaths && storePaths) override
    {
        if (substituterUris.empty())
            throw UsageError("you must specify at least one substituter using '-s'");

        // FIXME: factor out commonality with MixVerify.
        std::vector<ref<Store>> substituters;
        for (auto & s : substituterUris)
            substituters.push_back(openStore(s));

        ThreadPool pool{fileTransferSettings.httpConnections};

        std::atomic<size_t> added{0};

        // logger->setExpected(doneLabel, storePaths.size());

        auto doPath = [&](const Path & storePathS) {
            // Activity act(*logger, lvlInfo, "getting signatures for '%s'", storePath);

            checkInterrupt();

            auto storePath = store->parseStorePath(storePathS);

            auto info = store->queryPathInfo(storePath);

            StringSet newSigs;

            for (auto & store2 : substituters) {
                try {
                    auto info2 = store2->queryPathInfo(info->path);

                    /* Don't import signatures that don't match this
                       binary. */
                    if (info->narHash != info2->narHash || info->narSize != info2->narSize
                        || info->references != info2->references)
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

            // logger->incProgress(doneLabel);
        };

        for (auto & storePath : storePaths)
            pool.enqueue(std::bind(doPath, store->printStorePath(storePath)));

        pool.process();

        printInfo("imported %d signatures", added);
    }
};

static auto rCmdCopySigs = registerCommand2<CmdCopySigs>({"store", "copy-sigs"});

struct CmdSign : StorePathsCommand
{
    Path secretKeyFile;

    CmdSign()
    {
        addFlag({
            .longName = "key-file",
            .shortName = 'k',
            .description = "File containing the secret signing key.",
            .labels = {"file"},
            .handler = {&secretKeyFile},
            .completer = completePath,
            .required = true,
        });
    }

    std::string description() override
    {
        return "sign store paths with a local key";
    }

    void run(ref<Store> store, StorePaths && storePaths) override
    {
        SecretKey secretKey(readFile(secretKeyFile));
        LocalSigner signer(std::move(secretKey));

        size_t added{0};

        for (auto & storePath : storePaths) {
            auto info = store->queryPathInfo(storePath);

            auto info2(*info);
            info2.sigs.clear();
            info2.sign(*store, signer);
            assert(!info2.sigs.empty());

            if (!info->sigs.count(*info2.sigs.begin())) {
                store->addSignatures(storePath, info2.sigs);
                added++;
            }
        }

        printInfo("added %d signatures", added);
    }
};

static auto rCmdSign = registerCommand2<CmdSign>({"store", "sign"});

struct CmdKeyGenerateSecret : Command
{
    std::string keyName;

    CmdKeyGenerateSecret()
    {
        addFlag({
            .longName = "key-name",
            .description = "Identifier of the key (e.g. `cache.example.org-1`).",
            .labels = {"name"},
            .handler = {&keyName},
            .required = true,
        });
    }

    std::string description() override
    {
        return "generate a secret key for signing store paths";
    }

    std::string doc() override
    {
        return
#include "key-generate-secret.md"
            ;
    }

    void run() override
    {
        logger->stop();
        writeFull(getStandardOutput(), SecretKey::generate(keyName).to_string());
    }
};

struct CmdKeyConvertSecretToPublic : Command
{
    std::string description() override
    {
        return "generate a public key for verifying store paths from a secret key read from standard input";
    }

    std::string doc() override
    {
        return
#include "key-convert-secret-to-public.md"
            ;
    }

    void run() override
    {
        SecretKey secretKey(drainFD(STDIN_FILENO));
        logger->stop();
        writeFull(getStandardOutput(), secretKey.toPublicKey().to_string());
    }
};

struct CmdKey : NixMultiCommand
{
    CmdKey()
        : NixMultiCommand(
              "key",
              {
                  {"generate-secret", []() { return make_ref<CmdKeyGenerateSecret>(); }},
                  {"convert-secret-to-public", []() { return make_ref<CmdKeyConvertSecretToPublic>(); }},
              })
    {
    }

    std::string description() override
    {
        return "generate and convert Nix signing keys";
    }

    Category category() override
    {
        return catUtility;
    }
};

static auto rCmdKey = registerCommand<CmdKey>("key");
