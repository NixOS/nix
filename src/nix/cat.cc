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

        FdSink output{getStandardOutput()};
        accessor->readFile(path, output);
        output.flush();
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
        cat(store->requireStoreObjectAccessor(storePath), rest);
    }
};

struct CmdCatNar : StoreCommand, MixCat
{
    std::filesystem::path narPath;

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
        auto fd = openFileReadonly(narPath);
        if (!fd)
            throw NativeSysError("opening NAR file %s", PathFmt(narPath));
        auto source = FdSource{fd.get()};

        struct CatRegularFileSink : NullFileSystemObjectSink
        {
            CanonPath currentPath;
            CanonPath neededPath;
            bool & found;

            CatRegularFileSink(CanonPath currentPath, CanonPath neededPath, bool & found)
                : currentPath(std::move(currentPath))
                , neededPath(std::move(neededPath))
                , found(found)
            {
            }

            void createDirectory(DirectoryCreatedCallback callback) override
            {
                struct Dir : OnDirectory
                {
                    CatRegularFileSink & parent;

                    Dir(CatRegularFileSink & parent)
                        : parent(parent)
                    {
                    }

                    void createChild(std::string_view name, ChildCreatedCallback callback) override
                    {
                        CatRegularFileSink childSink{
                            parent.currentPath / std::string{name}, parent.neededPath, parent.found};
                        callback(childSink);
                    }
                } dir{*this};

                callback(dir);
            }

            void createRegularFile(bool isExecutable, RegularFileCreatedCallback crf) override
            {
                struct CRF : OnRegularFile, FdSink
                {
                } crfSink;

                crfSink.fd = INVALID_DESCRIPTOR;

                if (currentPath == neededPath) {
                    logger->stop();
                    crfSink.skipContents = false;
                    crfSink.fd = getStandardOutput();
                    found = true;
                } else {
                    crfSink.skipContents = true;
                }

                crf(crfSink);
            }
        };

        bool found = false;
        CatRegularFileSink sink{CanonPath::root, CanonPath(path), found};
        /* NOTE: We still parse the whole file to validate that it's a correct NAR. */
        parseDump(sink, source);

        if (!found)
            throw Error("NAR does not contain regular file '%1%'", path);
    }
};

static auto rCmdCatStore = registerCommand2<CmdCatStore>({"store", "cat"});
static auto rCmdCatNar = registerCommand2<CmdCatNar>({"nar", "cat"});
