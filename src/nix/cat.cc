#include "command.hh"
#include "store-api.hh"
#include "fs-accessor.hh"
#include "nar-accessor.hh"

#ifdef _WIN32
#include "builtins.hh"
#include "derivations.hh"
#include <nlohmann/json.hpp>
#endif

using namespace nix;

struct MixCat : virtual Args
{
    std::string path;

    void cat(ref<FSAccessor> accessor)
    {
        auto st = accessor->stat1(path);
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


struct CmdLn : Command
{
    Path target, link;

    CmdLn()
    {
        expectArg("target", &target);
        expectArg("link", &link);
    }

    std::string name() override
    {
        return "ln";
    }

    std::string description() override
    {
        return "make a symbolic link (because MSYS's ln does not do it)";
    }

    void run() override
    {
        createSymlink(target, absPath(link));
    }
};

static RegisterCommand r3(make_ref<CmdLn>());


#ifdef _WIN32
// builtin:fetchurl builder as there is no fork()
struct CmdBuiltinFetchurl : Command
{
    std::string drvenv;

    CmdBuiltinFetchurl()
    {
        expectArg("drvenv", &drvenv);
    }

    std::string name() override
    {
        return "builtin-fetchurl";
    }

    std::string description() override
    {
        return "an internal command supporting `builtin:fetchurl` builder";
    }

    void run() override
    {
        logger = makeJSONLogger(*logger);

        std::cerr << "drvenv=" << drvenv << std::endl;
        auto j = nlohmann::json::parse(drvenv);
        std::cerr << "j=" << j << std::endl;

        BasicDerivation drv2;
        for (nlohmann::json::iterator it = j.begin(); it != j.end(); ++it) {
          drv2.env[it.key()] = it.value();
        }
        builtinFetchurl(drv2, /*netrcData*/"");
    }
};

static RegisterCommand r4(make_ref<CmdBuiltinFetchurl>());
#endif