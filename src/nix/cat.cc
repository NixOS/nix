#include "nix/cmd/command.hh"
#include "nix/store/store-api.hh"
#include "nix/util/archive.hh"
#include "nix/util/nar-accessor.hh"
#include "nix/util/serialise.hh"
#include "nix/util/source-accessor.hh"

#include <nlohmann/json.hpp>

using namespace nix;

struct MixCat : virtual Args
{
    void cat(ref<SourceAccessor> accessor, CanonPath path)
    {
        auto st = accessor->lstat(path);
        if (st.type != SourceAccessor::Type::tRegular)
            throw Error("path '%1%' is not a regular file", path.abs());
        logger->stop();

        writeFull(getStandardOutput(), accessor->readFile(path));
    }
};

struct CmdCatStore : StoreCommand, MixCat
{
    std::string path;

    CmdCatStore()
    {
        expectArgs({.label = "path", .handler = {&path}, .completer = completePath});
    }

    std::string description() override
    {
        return "print the contents of a file in the Nix store on stdout";
    }

    std::string doc() override
    {
        return
#include "store-cat.md"
            ;
    }

    void run(ref<Store> store) override
    {
        auto [storePath, rest] = store->toStorePath(path);
        cat(store->requireStoreObjectAccessor(storePath), CanonPath{rest});
    }
};

struct CmdCatNar : StoreCommand, MixCat
{
    Path narPath;

    std::string path;

    CmdCatNar()
    {
        expectArgs({.label = "nar", .handler = {&narPath}, .completer = completePath});
        expectArg("path", &path);
    }

    std::string description() override
    {
        return "print the contents of a file inside a NAR file on stdout";
    }

    std::string doc() override
    {
        return
#include "nar-cat.md"
            ;
    }

    void run(ref<Store> store) override
    {
        AutoCloseFD fd = toDescriptor(open(narPath.c_str(), O_RDONLY));
        if (!fd)
            throw SysError("opening NAR file '%s'", narPath);
        auto source = FdSource{fd.get()};

        struct CatRegularFileSink : NullFileSystemObjectSink
        {
            CanonPath neededPath = CanonPath::root;
            bool found = false;

            void createRegularFile(const CanonPath & path, std::function<void(CreateRegularFileSink &)> crf) override
            {
                struct : CreateRegularFileSink, FdSink
                {
                    void isExecutable() override {}
                } crfSink;

                crfSink.fd = INVALID_DESCRIPTOR;

                if (path == neededPath) {
                    logger->stop();
                    crfSink.skipContents = false;
                    crfSink.fd = STDOUT_FILENO;
                    found = true;
                } else {
                    crfSink.skipContents = true;
                }

                crf(crfSink);
            }
        } sink;

        sink.neededPath = CanonPath(path);
        /* NOTE: We still parse the whole file to validate that it's a correct NAR. */
        parseDump(sink, source);

        if (!sink.found)
            throw Error("NAR does not contain regular file '%1%'", path);
    }
};

static auto rCmdCatStore = registerCommand2<CmdCatStore>({"store", "cat"});
static auto rCmdCatNar = registerCommand2<CmdCatNar>({"nar", "cat"});
