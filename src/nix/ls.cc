#include "command.hh"
#include "store-api.hh"
#include "fs-accessor.hh"
#include "nar-accessor.hh"
#include "common-args.hh"
#include "json.hh"

using namespace nix;

struct MixLs : virtual Args, MixJSON
{
    std::string path;

    bool recursive = false;
    bool verbose = false;
    bool showDirectory = false;

    MixLs()
    {
        mkFlag('R', "recursive", "list subdirectories recursively", &recursive);
        mkFlag('l', "long", "show more file information", &verbose);
        mkFlag('d', "directory", "show directories rather than their contents", &showDirectory);
    }

    void listText(ref<FSAccessor> accessor)
    {
        std::function<void(const FSAccessor::Stat &, const Path &, const std::string &, bool)> doPath;

        auto showFile = [&](const Path & curPath, const std::string & relPath) {
            if (verbose) {
                auto st = accessor->stat(curPath);
                std::string tp =
                    st.type == FSAccessor::Type::tRegular ?
                        (st.isExecutable ? "-r-xr-xr-x" : "-r--r--r--") :
                    st.type == FSAccessor::Type::tSymlink ? "lrwxrwxrwx" :
                    "dr-xr-xr-x";
                std::cout <<
                    (format("%s %20d %s") % tp % st.fileSize % relPath);
                if (st.type == FSAccessor::Type::tSymlink)
                    std::cout << " -> " << accessor->readLink(curPath)
                    ;
                std::cout << "\n";
                if (recursive && st.type == FSAccessor::Type::tDirectory)
                    doPath(st, curPath, relPath, false);
            } else {
                std::cout << relPath << "\n";
                if (recursive) {
                    auto st = accessor->stat(curPath);
                    if (st.type == FSAccessor::Type::tDirectory)
                        doPath(st, curPath, relPath, false);
                }
            }
        };

        doPath = [&](const FSAccessor::Stat & st, const Path & curPath,
            const std::string & relPath, bool showDirectory)
        {
            if (st.type == FSAccessor::Type::tDirectory && !showDirectory) {
                auto names = accessor->readDirectory(curPath);
                for (auto & name : names)
                    showFile(curPath + "/" + name, relPath + "/" + name);
            } else
                showFile(curPath, relPath);
        };

        auto st = accessor->stat(path);
        if (st.type == FSAccessor::Type::tMissing)
            throw Error(format("path '%1%' does not exist") % path);
        doPath(st, path,
            st.type == FSAccessor::Type::tDirectory ? "." : baseNameOf(path),
            showDirectory);
    }

    void list(ref<FSAccessor> accessor)
    {
        if (path == "/") path = "";

        if (json) {
            JSONPlaceholder jsonRoot(std::cout);
            listNar(jsonRoot, accessor, path, recursive);
        } else
            listText(accessor);
    }
};

struct CmdLsStore : StoreCommand, MixLs
{
    CmdLsStore()
    {
        expectArg("path", &path);
    }

    Examples examples() override
    {
        return {
            Example{
                "To list the contents of a store path in a binary cache:",
                "nix ls-store --store https://cache.nixos.org/ -lR /nix/store/0i2jd68mp5g6h2sa5k9c85rb80sn8hi9-hello-2.10"
            },
        };
    }

    std::string name() override
    {
        return "ls-store";
    }

    std::string description() override
    {
        return "show information about a store path";
    }

    void run(ref<Store> store) override
    {
        list(store->getFSAccessor());
    }
};

struct CmdLsNar : Command, MixLs
{
    Path narPath;

    CmdLsNar()
    {
        expectArg("nar", &narPath);
        expectArg("path", &path);
    }

    Examples examples() override
    {
        return {
            Example{
                "To list a specific file in a NAR:",
                "nix ls-nar -l hello.nar /bin/hello"
            },
        };
    }

    std::string name() override
    {
        return "ls-nar";
    }

    std::string description() override
    {
        return "show information about the contents of a NAR file";
    }

    void run() override
    {
        list(makeNarAccessor(make_ref<std::string>(readFile(narPath, true))));
    }
};

static RegisterCommand r1(make_ref<CmdLsStore>());
static RegisterCommand r2(make_ref<CmdLsNar>());
