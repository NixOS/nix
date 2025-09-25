#include "nix/cmd/command.hh"
#include "nix/main/shared.hh"
#include "nix/store/store-api.hh"
#include "nix/store/export-import.hh"
#include "nix/util/callback.hh"
#include "nix/util/fs-sink.hh"
#include "nix/util/archive.hh"

using namespace nix;

struct CmdNario : NixMultiCommand
{
    CmdNario()
        : NixMultiCommand("nario", RegisterCommand::getCommandsFor({"nario"}))
    {
    }

    std::string description() override
    {
        return "operations for manipulating nario files";
    }

    Category category() override
    {
        return catUtility;
    }
};

static auto rCmdNario = registerCommand<CmdNario>("nario");

struct CmdNarioExport : StorePathsCommand
{
    unsigned int version = 0;

    CmdNarioExport()
    {
        addFlag({
            .longName = "format",
            .description = "Version of the nario format to use. Must be `1`.",
            .labels = {"nario-format"},
            .handler = {&version},
        });
    }

    std::string description() override
    {
        return "serialize store paths to standard output in nario format";
    }

    std::string doc() override
    {
        return
#include "nario-export.md"
            ;
    }

    void run(ref<Store> store, StorePaths && storePaths) override
    {
        if (!version)
            throw UsageError("`nix nario export` requires `--format` argument");

        FdSink sink(getStandardOutput());
        exportPaths(*store, StorePathSet(storePaths.begin(), storePaths.end()), sink, version);
    }
};

static auto rCmdNarioExport = registerCommand2<CmdNarioExport>({"nario", "export"});

struct CmdNarioImport : StoreCommand
{
    std::string description() override
    {
        return "import store paths from a nario file on standard input";
    }

    std::string doc() override
    {
        return
#include "nario-import.md"
            ;
    }

    void run(ref<Store> store) override
    {
        FdSource source(getStandardInput());
        importPaths(*store, source, NoCheckSigs); // FIXME
    }
};

static auto rCmdNarioImport = registerCommand2<CmdNarioImport>({"nario", "import"});

struct CmdNarioList : Command
{
    std::string description() override
    {
        return "list the contents of a nario file";
    }

    std::string doc() override
    {
        return
#include "nario-list.md"
            ;
    }

    void run() override
    {
        struct Config : StoreConfig
        {
            Config(const Params & params)
                : StoreConfig(params)
            {
            }

            ref<Store> openStore() const override
            {
                abort();
            }
        };

        struct ListingStore : Store
        {
            ListingStore(ref<const Config> config)
                : Store{*config}
            {
            }

            void queryPathInfoUncached(
                const StorePath & path, Callback<std::shared_ptr<const ValidPathInfo>> callback) noexcept override
            {
                callback(nullptr);
            }

            std::optional<TrustedFlag> isTrustedClient() override
            {
                return Trusted;
            }

            std::optional<StorePath> queryPathFromHashPart(const std::string & hashPart) override
            {
                return std::nullopt;
            }

            void
            addToStore(const ValidPathInfo & info, Source & source, RepairFlag repair, CheckSigsFlag checkSigs) override
            {
                logger->cout(fmt("%s: %d bytes", printStorePath(info.path), info.narSize));
                // Discard the NAR.
                NullFileSystemObjectSink parseSink;
                parseDump(parseSink, source);
            }

            StorePath addToStoreFromDump(
                Source & dump,
                std::string_view name,
                FileSerialisationMethod dumpMethod,
                ContentAddressMethod hashMethod,
                HashAlgorithm hashAlgo,
                const StorePathSet & references,
                RepairFlag repair) override
            {
                unsupported("addToStoreFromDump");
            }

            void narFromPath(const StorePath & path, Sink & sink) override
            {
                unsupported("narFromPath");
            }

            void queryRealisationUncached(
                const DrvOutput &, Callback<std::shared_ptr<const Realisation>> callback) noexcept override
            {
                callback(nullptr);
            }

            ref<SourceAccessor> getFSAccessor(bool requireValidPath) override
            {
                return makeEmptySourceAccessor();
            }
        };

        FdSource source(getStandardInput());
        auto config = make_ref<Config>(StoreConfig::Params());
        ListingStore lister(config);
        importPaths(lister, source, NoCheckSigs);
    }
};

static auto rCmdNarioList = registerCommand2<CmdNarioList>({"nario", "list"});
