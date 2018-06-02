#include "command.hh"
#include "shared.hh"
#include "store-api.hh"
#include "sync.hh"
#include "thread-pool.hh"

#include <atomic>

using namespace nix;

struct CmdCopy : StoreCommand
{
    std::string srcUri, dstUri;
    std::vector<std::string> srcPaths;

    CheckSigsFlag checkSigs = CheckSigs;

    SubstituteFlag substitute = NoSubstitute;

    CmdCopy() : StoreCommand()
    {
        mkFlag()
            .longName("from")
            .labels({"store-uri"})
            .description("URI of the source Nix store")
            .dest(&srcUri);
        mkFlag()
            .longName("to")
            .labels({"store-uri"})
            .description("URI of the destination Nix store")
            .dest(&dstUri);

        mkFlag()
            .longName("no-check-sigs")
            .description("do not require that paths are signed by trusted keys")
            .set(&checkSigs, NoCheckSigs);

        mkFlag()
            .longName("substitute")
            .shortName('s')
            .description("whether to try substitutes on the destination store (only supported by SSH)")
            .set(&substitute, Substitute);

        expectArgs("paths", &srcPaths);
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
                "To populate the current folder build output to a S3 binary cache:",
                "nix copy --to s3://my-bucket?region=eu-west-1"
            },
#endif
        };
    }

    ref<Store> createStore() override
    {
        return srcUri.empty() ? StoreCommand::createStore() : openStore(srcUri);
    }

    void run(ref<Store> srcStore) override
    {
        if (srcUri.empty() && dstUri.empty())
            throw UsageError("you must pass '--from' and/or '--to'");

        ref<Store> dstStore = dstUri.empty() ? openStore() : openStore(dstUri);

        PathSet closure;
        srcStore->computeFSClosure(PathSet(srcPaths.begin(), srcPaths.end()), closure, false, false);
        copyPaths(srcStore, dstStore, closure, NoRepair, checkSigs, substitute);
    }
};

static RegisterCommand r1(make_ref<CmdCopy>());
