#include "command.hh"
#include "store-api.hh"

using namespace nix;

struct MixImportExport : virtual Args
{
    enum struct ArchiveFormat
    {
        Binary,
    };

    std::optional<ArchiveFormat> format = std::nullopt;
    MixImportExport()
    {
        addFlag(
            {.longName = "format",
             .description = R"(
                Format of the archive.
                The only supported format is `binary`, which corresponds to the format used by [`nix-store --export`](@docroot@/command-ref/nix-store/export.md).
            )",
             .labels = {"format"},
             .handler = {[&](std::string_view arg) {
                 if (arg == "binary")
                     format = ArchiveFormat::Binary;
                 else
                     throw Error("Unknown archive format: %s", arg);
             }}});
    }

};

struct CmdStoreExport : StorePathsCommand, MixImportExport
{
    std::string description() override
    {
        return "Export the given store path(s) in a way that can be imported by `nix store import`.";
    }

    std::string doc() override
    {
        return
            #include "store-export.md"
        ;
    }

    std::optional<Path> outputFile = std::nullopt;

    CmdStoreExport()
    {
        addFlag({
            .longName = "output-file",
            .description = "Write the archive to the given file instead of stdout.",
            .labels = {"file"},
            .handler = {&outputFile},
        });

    }

    void run(ref<Store> store, StorePaths && storePaths) override
    {

        // We don't use the format parameter now, but we still want to enforce it
        // being specified to not block us on backwards-compatibility.
        if (!format)
            throw UsageError("`--format` must be specified");

        StorePathSet pathsAsSet;
        pathsAsSet.insert(storePaths.begin(), storePaths.end());

        auto sink = [&]() -> FdSink {
            if (outputFile) {
                if (*outputFile == "-") {
                    return FdSink(STDOUT_FILENO);
                } else {
                    auto fd = open(outputFile->c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
                    return FdSink(fd);
                }
            } else if (isatty(STDOUT_FILENO)) {
                throw Error("Refusing to write binary data to a terminal. Use `--output-file` to specify a file to write to.");
            } else {
                return FdSink(STDOUT_FILENO);
            }
        }();

        store->exportPaths(pathsAsSet, sink);
        sink.flush();
    }
};

struct CmdStoreImport : StoreCommand, MixImportExport
{
    std::string description() override
    {
        return "Import the given store path(s) from a file created by `nix store export`.";
    }
    std::string doc() override
    {
        return
            #include "store-import.md"
        ;
    }

    std::optional<Path> inputFile = std::nullopt;

    CmdStoreImport() {
        addFlag({
            .longName = "input-file",
            .description = "Read the archive from the given file instead of stdin.",
            .labels = {"file"},
            .handler = {&inputFile},
        });
    }

    void run(ref<Store> store) override
    {
        FdSource source = [&]() -> FdSource {
            if (inputFile && *inputFile != "-") {
                auto fd = open(inputFile->c_str(), O_RDONLY);
                return FdSource(fd);
            } else {
                return FdSource(STDIN_FILENO);
            }
        }();
        auto paths = store->importPaths(source, NoCheckSigs);

        for (auto & path : paths)
            logger->cout("%s", store->printStorePath(path));
    }

};

static auto rStoreExport = registerCommand2<CmdStoreExport>({"store", "export"});
static auto rStoreImport = registerCommand2<CmdStoreImport>({"store", "import"});
