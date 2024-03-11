#include "command.hh"
#include "common-args.hh"
#include "store-api.hh"
#include "archive.hh"
#include "git.hh"
#include "posix-source-accessor.hh"
#include "misc-store-flags.hh"

using namespace nix;

struct CmdAddToStore : MixDryRun, StoreCommand
{
    Path path;
    std::optional<std::string> namePart;
    ContentAddressMethod caMethod = FileIngestionMethod::Recursive;
    HashAlgorithm hashAlgo = HashAlgorithm::SHA256;

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
    }

    void run(ref<Store> store) override
    {
        if (!namePart) namePart = baseNameOf(path);

        auto [accessor, path2] = PosixSourceAccessor::createAtRoot(path);

        auto storePath = dryRun
            ? store->computeStorePath(
                *namePart, accessor, path2, caMethod, hashAlgo, {}).first
            : store->addToStoreSlow(
                *namePart, accessor, path2, caMethod, hashAlgo, {}).path;

        logger->cout("%s", store->printStorePath(storePath));
    }
};

struct CmdAdd : CmdAddToStore
{
    std::string description() override
    {
        return "Add a file or directory to the Nix store";
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
        caMethod = FileIngestionMethod::Flat;
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
