#include "command.hh"
#include "shared.hh"
#include "store-api.hh"
#include "local-fs-store.hh"

using namespace nix;

struct CmdCopy : virtual CopyCommand, virtual BuiltPathsCommand, MixProfile
{
    std::optional<std::filesystem::path> outLink;
    CheckSigsFlag checkSigs = CheckSigs;

    SubstituteFlag substitute = NoSubstitute;

    CmdCopy()
        : BuiltPathsCommand(true)
    {
        addFlag({
            .longName = "out-link",
            .shortName = 'o',
            .description = "Create symlinks prefixed with *path* to the top-level store paths fetched from the source store.",
            .labels = {"path"},
            .handler = {&outLink},
            .completer = completePath
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

    void run(ref<Store> srcStore, BuiltPaths && allPaths, BuiltPaths && rootPaths) override
    {
        auto dstStore = getDstStore();

        RealisedPath::Set stuffToCopy;

        for (auto & builtPath : allPaths) {
            auto theseRealisations = builtPath.toRealisedPaths(*srcStore);
            stuffToCopy.insert(theseRealisations.begin(), theseRealisations.end());
        }

        copyPaths(
            *srcStore, *dstStore, stuffToCopy, NoRepair, checkSigs, substitute);

        updateProfile(rootPaths);

        if (outLink) {
            if (auto store2 = dstStore.dynamic_pointer_cast<LocalFSStore>())
                createOutLinks(*outLink, rootPaths, *store2);
            else
                throw Error("'--out-link' is not supported for this Nix store");
        }
    }
};

static auto rCmdCopy = registerCommand<CmdCopy>("copy");
