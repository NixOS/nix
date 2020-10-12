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

    SubstituteFlag substitute = NoSubstitute;

    CmdCopy()
        : StorePathsCommand(true)
    {
        addFlag({
            .longName = "from",
            .description = "URI of the source Nix store",
            .labels = {"store-uri"},
            .handler = {&srcUri},
        });

        addFlag({
            .longName = "to",
            .description = "URI of the destination Nix store",
            .labels = {"store-uri"},
            .handler = {&dstUri},
        });

        addFlag({
            .longName = "no-check-sigs",
            .description = "do not require that paths are signed by trusted keys",
            .handler = {&checkSigs, NoCheckSigs},
        });

        addFlag({
            .longName = "substitute-on-destination",
            .shortName = 's',
            .description = "whether to try substitutes on the destination store (only supported by SSH)",
            .handler = {&substitute, Substitute},
        });

        realiseMode = Realise::Outputs;
    }

    std::string description() override
    {
        return "copy paths between Nix stores";
    }

    Examples examples() override
    {
        return {
            Example{
                "To copy Firefox from the local store to a binary cache in file:///tmp/cache:",
                "nix copy --to file:///tmp/cache $(type -p firefox)"
            },
            Example{
                "To copy the entire current NixOS system closure to another machine via SSH:",
                "nix copy --to ssh://server /run/current-system"
            },
            Example{
                "To copy a closure from another machine via SSH:",
                "nix copy --from ssh://server /nix/store/a6cnl93nk1wxnq84brbbwr6hxw9gp2w9-blender-2.79-rc2"
            },
#ifdef ENABLE_S3
            Example{
                "To copy Hello to an S3 binary cache:",
                "nix copy --to s3://my-bucket?region=eu-west-1 nixpkgs#hello"
            },
            Example{
                "To copy Hello to an S3-compatible binary cache:",
                "nix copy --to s3://my-bucket?region=eu-west-1&endpoint=example.com nixpkgs#hello"
            },
#endif
        };
    }

    Category category() override { return catSecondary; }

    ref<Store> createStore() override
    {
        return srcUri.empty() ? StoreCommand::createStore() : openStore(srcUri);
    }

    void run(ref<Store> store) override
    {
        if (srcUri.empty() && dstUri.empty())
            throw UsageError("you must pass '--from' and/or '--to'");

        StorePathsCommand::run(store);
    }

    void run(ref<Store> srcStore, StorePaths storePaths) override
    {
        ref<Store> dstStore = dstUri.empty() ? openStore() : openStore(dstUri);

        copyPaths(srcStore, dstStore, StorePathSet(storePaths.begin(), storePaths.end()),
            NoRepair, checkSigs, substitute);
    }
};

static auto rCmdCopy = registerCommand<CmdCopy>("copy");
