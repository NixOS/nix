#include "nix/cmd/command.hh"
#include "nix/main/common-args.hh"
#include "nix/store/store-api.hh"
#include "nix/util/source-accessor.hh"
#include "nix/store/store-cast.hh"
#include "nix/store/submit-store.hh"
#include "nix/util/file-system.hh"
#include "nix/cmd/misc-store-flags.hh"

namespace nix {

struct CmdAddToStore : MixDryRun, StoreCommand
{
    std::filesystem::path path;
    std::optional<std::string> namePart;
    ContentAddressMethod caMethod = ContentAddressMethod::Raw::NixArchive;
    HashAlgorithm hashAlgo = HashAlgorithm::SHA256;
    bool scan = false;

    CmdAddToStore()
    {
        // FIXME: completion
        expectArg("path", &path);

        addFlag({
            .longName = "name",
            .shortName = 'n',
            .description = "Override the name component of the store path. It defaults to the base name of *path*.",
            .labels = {"name"},
            .handler = {&namePart},
        });

        addFlag(flag::contentAddressMethod(&caMethod));

        addFlag(flag::hashAlgo(&hashAlgo));

        addFlag({
            .longName = "scan",
            .description = "Scan for references. Only works within a `builder-rpc-v0` derivation.",
            .handler = {&scan, true},
        });
    }

    void run(ref<Store> store) override
    {
        // Although this would be convenient, if we are scanning then we are connecting to a daemon.
        // A dry-run scan would require either daemon-support for scanning a path for references
        // or listing referenceable paths, both of which come with downsides.
        if (dryRun && scan)
            throw UsageError("Cannot dry-run while scanning");

        if (!namePart)
            namePart = path.filename().string();

        auto sourcePath = makeFSSourceAccessor(absPath(path));

        auto storePath = ([&]() {
            if (scan) {
                auto & submitStore = require<SubmitStore>(*store);

                auto serialisationMethod = caMethod.getFileSerialisationMethod();

                std::optional<StorePath> storePath;
                auto sink = sourceToSink([&](Source & source) {
                    auto info =
                        submitStore.addToStoreScanning(source, *namePart, serialisationMethod, caMethod, hashAlgo);
                    storePath = info->path;
                });
                dumpPath(sourcePath, *sink, serialisationMethod, defaultPathFilter);
                sink->finish();

                return storePath.value();
            } else if (dryRun) {
                return store->computeStorePath(*namePart, sourcePath, caMethod, hashAlgo, {}).first;
            } else {
                return store->addToStoreSlow(*namePart, sourcePath, caMethod, hashAlgo, {}).path;
            }
        })();
        logger->cout("%s", store->printStorePath(storePath));
    }
};

struct CmdAdd : CmdAddToStore
{
    std::string description() override
    {
        return "add a file or directory to the Nix store";
    }

    std::string doc() override
    {
        return
#include "add.md"
            ;
    }
};

struct CmdAddFile : CmdAddToStore
{
    CmdAddFile()
    {
        caMethod = ContentAddressMethod::Raw::Flat;
    }

    std::string description() override
    {
        return "Deprecated. Use [`nix store add --mode flat`](@docroot@/command-ref/new-cli/nix3-store-add.md) instead.";
    }
};

struct CmdAddPath : CmdAddToStore
{
    std::string description() override
    {
        return "Deprecated alias to [`nix store add`](@docroot@/command-ref/new-cli/nix3-store-add.md).";
    }
};

static auto rCmdAddFile = registerCommand2<CmdAddFile>({"store", "add-file"});
static auto rCmdAddPath = registerCommand2<CmdAddPath>({"store", "add-path"});
static auto rCmdAdd = registerCommand2<CmdAdd>({"store", "add"});

} // namespace nix
