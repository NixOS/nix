#include "command.hh"
#include "store-api.hh"
#include "fs-accessor.hh"
#include "nar-accessor.hh"

using namespace nix;

struct MixCat : virtual Args
{
    std::string path;

    void cat(ref<FSAccessor> accessor)
    {
        auto st = accessor->stat(path);
        if (st.type == FSAccessor::Type::tMissing)
            throw Error(format("path '%1%' does not exist") % path);
        if (st.type != FSAccessor::Type::tRegular)
            throw Error(format("path '%1%' is not a regular file") % path);

        std::cout << accessor->readFile(path);
    }
};

struct CmdCatStore : StoreCommand, MixCat
{
    CmdCatStore()
    {
        expectArg("path", &path);
    }

    std::string name() override
    {
        return "cat-store";
    }

    std::string description() override
    {
        return "print the contents of a store file on stdout";
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
        expectArg("nar", &narPath);
        expectArg("path", &path);
    }

    std::string name() override
    {
        return "cat-nar";
    }

    std::string description() override
    {
        return "print the contents of a file inside a NAR file";
    }

    void run(ref<Store> store) override
    {
        cat(makeNarAccessor(make_ref<std::string>(readFile(narPath))));
    }
};

static RegisterCommand r1(make_ref<CmdCatStore>());
static RegisterCommand r2(make_ref<CmdCatNar>());
