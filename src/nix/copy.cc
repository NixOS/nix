#include "command.hh"
#include "shared.hh"
#include "store-api.hh"
#include "sync.hh"
#include "thread-pool.hh"

#include <atomic>

using namespace nix;

struct CmdCopy : BuiltPathsCommand
{
    std::string srcUri, dstUri;

    CheckSigsFlag checkSigs = CheckSigs;

    SubstituteFlag substitute = NoSubstitute;

    using BuiltPathsCommand::run;

    CmdCopy()
        : BuiltPathsCommand(true)
    {
        addFlag({
            .longName = "from",
            .description = "URL of the source Nix store.",
            .labels = {"store-uri"},
            .handler = {&srcUri},
        });

        addFlag({
            .longName = "to",
            .description = "URL of the destination Nix store.",
            .labels = {"store-uri"},
            .handler = {&dstUri},
        });

        addFlag({
            .longName = "no-check-sigs",
            .description = "Do not require that paths are signed by trusted keys.",
            .handler = {&checkSigs, NoCheckSigs},
        });

        addFlag({
            .longName = "substitute-on-destination",
            .shortName = 's',
            .description = "Whether to try substitutes on the destination store (only supported by SSH stores).",
            .handler = {&substitute, Substitute},
        });

        realiseMode = Realise::Outputs;
    }

    std::string description() override
    {
        return "copy paths between Nix stores";
    }

    std::string doc() override
    {
        return
          #include "copy.md"
          ;
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

        BuiltPathsCommand::run(store);
    }

    void run(ref<Store> srcStore, BuiltPaths paths) override
    {
        ref<Store> dstStore = dstUri.empty() ? openStore() : openStore(dstUri);

        RealisedPath::Set stuffToCopy;

        for (auto & builtPath : paths) {
            auto theseRealisations = builtPath.toRealisedPaths(*srcStore);
            stuffToCopy.insert(theseRealisations.begin(), theseRealisations.end());
        }

        copyPaths(
            *srcStore, *dstStore, stuffToCopy, NoRepair, checkSigs, substitute);
    }
};

static auto rCmdCopy = registerCommand<CmdCopy>("copy");
