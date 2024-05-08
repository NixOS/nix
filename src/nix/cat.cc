#include "command.hh"
#include "store-api.hh"
#include "nar-accessor.hh"
#include "progress-bar.hh"

using namespace nix;

struct MixCat : virtual Args
{
    std::string path;

    void cat(ref<SourceAccessor> accessor)
    {
        auto st = accessor->lstat(CanonPath(path));
        if (st.type != SourceAccessor::Type::tRegular)
            throw Error("path '%1%' is not a regular file", path);
        stopProgressBar();

        writeFull(getStandardOut(), accessor->readFile(CanonPath(path)));
    }
};

struct CmdCatStore : StoreCommand, MixCat
{
    CmdCatStore()
    {
        expectArgs({
            .label = "path",
            .handler = {&path},
            .completer = completePath
        });
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
        cat(store->getFSAccessor());
    }
};

struct CmdCatNar : StoreCommand, MixCat
{
    Path narPath;

    CmdCatNar()
    {
        expectArgs({
            .label = "nar",
            .handler = {&narPath},
            .completer = completePath
        });
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
        cat(makeNarAccessor(readFile(narPath)));
    }
};

static auto rCmdCatStore = registerCommand2<CmdCatStore>({"store", "cat"});
static auto rCmdCatNar = registerCommand2<CmdCatNar>({"nar", "cat"});
